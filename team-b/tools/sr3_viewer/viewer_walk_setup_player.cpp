// runWalkDemo() split: the simulated player and the follow camera.
// Code moved verbatim out of the former single-function runWalkDemo() (see viewer_walk_session.h).
#include "viewer_platform.h"
#include "viewer_walk_session.h"

namespace sr3viewer {

int walkSetupPlayerAndCamera(WalkSession& sess) {
    auto& miscTablesPath = sess.miscTablesPath;
    auto& v2 = sess.v2;
    auto& docOf = sess.docOf;
    auto& src = sess.src;
    auto& stamina = sess.stamina;
    auto& heldFacing = sess.heldFacing;
    auto& lookSrc = sess.lookSrc;
    auto& followCamCollision = sess.followCamCollision;
    // ---- the simulated player ---------------------------------------------------
    sess.pm_.emplace(src, stamina);
    auto& pm = *sess.pm_;
    sr3player::SeedOnFootCommonCase(pm.world);
    pm.world.controlsDisabled.set(false);
    pm.world.movementDisabled.set(false);
    pm.world.permissions2078.set(0x4000u | 0x2000u);
    // SYNTHETIC tweak values - copied verbatim from
    // tests/synthetic_player_movement_test.cpp's own Rig (test arithmetic
    // only, not recovered engine constants; no tweak_table.xtbl load is
    // done by this demo).
    pm.tweaks.maxRunSpeed.set(10);
    pm.tweaks.maxSprintSpeed.set(15);
    pm.tweaks.crouchMoveSpeed.set(4);
    pm.tweaks.maxFineAimSpeed.set(3);
    pm.tweaks.acceleration.set(20);
    pm.tweaks.diveRollAcceleration.set(40);
    pm.tweaks.crouchClickSensitivity.set(0.5);
    // CHOSEN start position: the city_corner golden's own named "focus"
    // corner (tools/golden_scene_check.cpp's checkCityCornerScene() top
    // comment literally calls (-960,-700) "the box-overlap corner at focus"
    // where 4 real hN cells meet), y=0 matching every streamed prop's own
    // fake local-grid Y placement (loadCell()'s own buildWorldMatrix calls
    // always pass 0.0f for Y). [UPDATE 2026-10-07, ground/terrain follow-up
    // task: this project now CAN render a tile's real ground/terrain mesh
    // (CityGroundTile, see its own "GROUND/TERRAIN" top comment) WHEREVER
    // one exists in the real data - confirmed working against
    // tests/golden/zone_tile's own real sample (`sr3_city~f0816~al.czn_pc`,
    // a real located+rendered Mesh sub-block). CORRECTED same day, after an
    // earlier draft of this comment overclaimed it for THIS tile
    // specifically before actually checking: this exact corner's three
    // resident tiles (0916/1015/1016, confirmed via sr3_viewer's own `zone`
    // command - the SAME already-validated sr3zone::ZoneGeometry::locate()
    // pipeline tests/golden/zone_tile freezes) each have a real, substantial
    // `.czn_pc` (268-407 KB, not a stub) but ZERO real Mesh sub-blocks
    // located in any of the three BASE zones (0 blocks each; `.gzn_pc`
    // reads back 0 bytes for all three, i.e. absent or empty-shipped, and
    // none of the three carries a g-independent inline Mesh block either) -
    // a genuine, checked absence of terrain geometry in the BASE zone at
    // this specific real-world location, not a reader bug (see this task's
    // own report for the full population context: spec-terrain-format.md
    // §1 - CORRECTED 2026-10-08, this citation previously conflated two
    // different counts - records that 1,083/2,971 tiles ship a `.gzn_pc`
    // FILE AT ALL, of which 81 are zero bytes, leaving exactly 1,002
    // non-empty pairs (spec-zone-data-format.md §7.1; this project's own
    // HANDOFF.md §9.55.1 independently counts the same 1,002); so most
    // ordinary city tiles - this one's BASE zone included - simply have no
    // terrain-system geometry there. CORRECTED 2026-10-08 (adversarial
    // review follow-up): "no real asset here to draw" below is about the
    // BASE zone specifically and was previously overstated as there being
    // no real zone/terrain data here at all - there is: each of this
    // corner's 3 tiles' `~al` zone (nested in awld_compact.str2_pc, not
    // examined before findTileZoneEntryName()'s own 2026-10-08 fix - see
    // its "CORRECTED AGAIN" paragraph) carries 2 real Mesh sub-blocks,
    // re-derived directly against the real archives this pass. Still not
    // rendered here, and still deliberately so (see that same function's
    // "DECISION" paragraph): those blocks' own measured position bounding
    // boxes are too TALL (Y extents up to 116m) to be a flat street-level
    // surface, reading as world-level LOD-0 content (rooftops/skyline-type
    // geometry) rather than this corner's own walkable ground - an OPEN
    // spec-sync question for Team A, not resolved here. Steelport's
    // street-level "ground" at most locations is apparently carried some
    // other way, most plausibly as ordinary `.clmesh_pc` street/sidewalk
    // props rather than this heightmap-shaped terrain system. So
    // city_corner/walk_demo_v2's own void area around brad's feet is NOT
    // fixed by this task's own mechanism - correctly so, since there is no
    // real GROUND-SHAPED asset here to draw; inventing one (or promoting
    // the `~al` zone's differently-shaped geometry to fill the gap) would
    // be exactly the fabrication this project's "no invented fixes"
    // discipline forbids. Separately and NOT this task's own scope: a few
    // nearby `.clmesh_pc` props at this corner
    // (`hwy_yellow_st_r`, `hwy_sw`/`hwy_sw_l`/`hwy_sw_r`,
    // `hwy_yellow_ap_r` - plausible street/ramp-surface pieces by name) are
    // silently SKIPPED by the existing prop loader with "segment does not
    // open with the block's check value" - the SAME already-documented,
    // already-triaged `.clmesh_pc`/MeshBlock multi-reference-chain parse
    // limitation this project's own HANDOFF.md already tracks and has
    // flagged for a Team A disassembly cross-check (unrelated to the zone/
    // terrain system this task owns, and already present, unaffected, in
    // both the old and new build - not introduced or fixed by this task).
    // Also still true regardless: brad's own Y is a CHOSEN constant 0, not
    // sampled from any real mesh's own height at (x,z) - there is no
    // terrain-height-query mechanism anywhere in this project.]
    pm.warp(sr3player::Vec3{-960.0, 0.0, -700.0});
    // heldFacing{0, 0, 1} is the WalkSession member initialiser: CHOSEN initial facing (+z) - matches the fixed camera-row convention below

    // ---- the follow camera (Judgement call 1 above): TARGET only -------------
    sess.followCam_.emplace(lookSrc);
    auto& followCam = *sess.followCam_;
    sr3player::SeedFollowCameraOnFootCommonCase(followCam);
    // FIX 2 setup (--v2 only, see runWalkDemo()'s own "V2" doc comment):
    // seed everything SeedFollowCameraOnFootCommonCase() leaves OPEN for the
    // plain on-foot common case, then run the real init()/reset() so the
    // camera is genuinely placed behind brad (followSnap(), inside reset())
    // before the frame loop's own preUpdate()/update() calls take over.
    // `followCamCollision` is declared here (not inside this `if`) so it
    // outlives the frame loop below - CameraSystem::collision is a raw
    // pointer, never owning. CHOSEN default (no collision world at all) -
    // overridden below, AFTER `loaded`/RealCameraCollision exist, ONLY when
    // --collision is also passed (search "realCameraCollision" later in
    // this function for that real-geometry wiring); without --collision
    // this stays the active stand-in, unchanged.
    if (v2) {
        followCam.world.controlsDisabled.set(false);
        followCam.world.sprintPadDescriptorByte5.set(false);
        followCam.target.sizeFactor.set(1.0);     // CHOSEN: no human-size scaling data in this demo
        followCam.target.noCollision.set(false);  // exercise the real collision-resolve code path (see below)
        followCam.collision = &followCamCollision; // CHOSEN: no collision world in this project (camera.h's own opt-in)
        // Seed pm.world.orientation BEFORE the first DriveCameraTarget() call
        // below - it is an OpenValue<Orientation> (movement.h) that the frame
        // loop itself only .set()s once per frame (from pm.st.facingA50, or
        // heldFacing while facingA50 is exactly zero, as it is this early) -
        // DriveCameraTarget() calls pm.world.orientation.get() unconditionally,
        // so seeding it with the SAME heldFacing the loop's own frame-0 logic
        // would use keeps this one-time pre-loop call consistent with it.
        pm.world.orientation.set(sr3playeranim::OrientationFromFacing(heldFacing));
        // Seed the target BEFORE init()/reset() - reset()'s own doc comment:
        // "target must be set to the local player" (its snap reads target
        // NOW). pm is already warp()'d to its start position above.
        sr3playeranim::DriveCameraTarget(followCam, pm);
        // Real camera_free.xtbl (misc_tables.vpp_pc - the SAME archive
        // already open for the node-graph tables above; docOf() is the same
        // helper already used for control_parameters.xtbl etc.).
        const sr3xtbl::Document cameraFreeDoc = docOf("camera_free.xtbl");
        const auto cameraFreeParsed = sr3tables_environment::ParseCameraFree(cameraFreeDoc);
        if (!cameraFreeParsed) {
            std::fprintf(stderr, "could not parse camera_free.xtbl from %s\n", miscTablesPath.c_str());
            return 1;
        }
        const sr3player::CameraFreeSeedReport camSeedReport =
            sr3player::SeedCameraFromCameraFree(followCam, *cameraFreeParsed, /*widescreen=*/false);
        std::printf("camera   : camera_free.xtbl seeded (%d scalars, %d submode row(s) applied, %d incomplete, "
                    "%d unknown-name, %d aspect-filtered)\n",
                    camSeedReport.scalarsSeeded, camSeedReport.submodesSeeded, camSeedReport.submodesIncomplete,
                    camSeedReport.submodesUnknownName, camSeedReport.submodesAspectFiltered);
        followCam.init();
        followCam.reset();
        const auto& camNow = followCam.committed();
        std::printf("camera   : real follow-camera pipeline active (--v2); committed eye after reset (%.2f %.2f "
                    "%.2f), fov %.2f deg\n",
                    camNow.position.x, camNow.position.y, camNow.position.z, camNow.fov);
    }
    return -1;
}

}  // namespace sr3viewer
