#pragma once

#include "viewer_platform.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "sr3player/ground_follow.h"
#include "vpp/container.h"
#include "sr3playeranim/bridge.h"
#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"
#include "sr3player/camera_free_seed.h"
#include "sr3player/character_collision.h"
#include "sr3player/seated_ride_pose.h"

namespace sr3viewer {

// ===========================================================================
// `walk` (2026-10-07) - "a character walks": drives the real
// sr3player::PlayerMovement <-> sr3playeranim::PlayerAnimBridge <->
// sr3animruntime "player state machine" node graph through a short SCRIPTED
// on-foot sequence, placed on the real city_corner ground (the SAME cells/
// textures/camera framing as the `city` command's own frozen golden), with
// the real follow camera's (sr3player::CameraSystem) target kept in sync
// every frame. This is the viewer-demo counterpart to the `sr3playeranim`
// library merged separately (include/sr3playeranim/bridge.h) - this command
// does not add any new engine behaviour of its own, it only wires already-
// real units together and renders the result.
//
// REAL (every frame, not faked for the demo):
//   * the 'player state machine' node graph, loaded from the real
//     misc_tables.vpp_pc (control_parameters.xtbl/control_filters.xtbl/
//     anim_states.xtbl/anim_actions.xtbl/anim_triggers.xtbl/
//     node_graph_files.xtbl) through the SAME sr3animruntime::LoadNodeGraphs
//     tests/real_anim_node_graph_test.cpp exercises against the full
//     249-graph population (that test's own run needs no patch overlay to
//     reach all 249 - "player state machine" is an ordinary row, not the
//     one patched graph - so this command reads misc_tables.vpp_pc alone);
//   * sr3playeranim::PlayerAnimBridge::stepFrame() every frame: parameter
//     write -> graph evaluation (both passes) -> the T/E post-step split ->
//     playback-rate update -> compose/integrate/publish, in the real
//     engine's own order (spec-player-runtime.md Sec5.1), through
//     sr3player::PlayerMovement's real individual phases;
//   * the ground: runCity()'s own real hN-cell streaming/reconcile loop and
//     real per-material textures, byte-for-byte reused (same lambdas, same
//     helper functions - see the two JUDGEMENT CALLS below for the one
//     thing NOT reused from `city`);
//   * brad's real rigged mesh (characters.vpp_pc: brad.ccmesh_pc/
//     brad.gcmesh_pc/brad.rig_pc), loaded exactly like runPose() loads it;
//   * the follow camera's TARGET: sr3playeranim::DriveCameraTarget() is
//     called every frame against a real sr3player::CameraSystem, so
//     cam.target.referencePoint/orientation/heading genuinely track the
//     bridge-driven character's real simulated position/facing each frame -
//     this is the real hookup bridge.h's own file banner describes
//     ("Camera hookup... populates CameraSystem::target... just field
//     assignment, no new behaviour").
//
// JUDGEMENT CALL 1 - the follow camera's OWN mode-stack/submode-blend
// pipeline (CameraSystem::init/push/preUpdate/update) is deliberately NOT
// invoked here BY DEFAULT, even though its TARGET genuinely is driven every
// frame (see above). Running the real pipeline needs camera_free.xtbl's
// submode table (CameraSystem::submodes[], all 61 slots OPEN until seeded)
// and a collision sweep query (CameraCollision, OPEN when null) - neither is
// needed to show the player<->animation coupling working, and pulling them
// in would make this command about the camera subsystem, not the walk demo
// the task scoped. CHOSEN (default, no --v2): this command's own RENDERING
// uses the SAME fixed debug fly-camera framing tests/golden/city_corner
// already froze (--eye -960 250 -700 --yaw 0 --pitch -75 by default), not
// the follow camera's own (never-computed) position - so "the follow camera
// tracking it" is real and verifiable (cam.target is printed every capture,
// and visibly changes frame to frame), but the screenshot itself is still
// from the SAME known-good vantage point as every other `city`-family
// golden. walk_demo v2 (--v2, 2026-10-07 follow-up task; see the "V2" doc
// block below) lifts this specific judgement call for the on-foot/no-
// vehicle/no-weapon common case this demo already is - the real pipeline is
// genuinely run and ITS OWN committed output is what the v2 capture renders
// from - but ONLY under that opt-in flag; without it this paragraph (and
// tests/golden/walk_demo's own frozen baseline) is unchanged.
//
// JUDGEMENT CALL 2 - the character's own visual pose is a RIGID BODY, not a
// re-skinned animation. Only ONE skin (the bind pose, identity bone
// rotations) is ever computed and uploaded to the GPU; each frame's render
// just moves/rotates that SAME static mesh with a plain world matrix built
// from the bridge-driven simulation's own position + facing
// (sr3render::buildWorldMatrix(pos.x, pos.y, pos.z,
// HeadingFromForward(forward), 1.0, ...) - confirmed algebraically to use
// the SAME yaw convention as sr3player::RotationFromAngles/
// HeadingFromForward: buildWorldMatrix's local +Z axis maps to world
// (sin(yaw), 0, cos(yaw)), exactly RotationFromAngles(0,0,heading).forward,
// so heading needs no sign flip). This project has no real per-clip
// keyframe/track sampler for `.anim_pc` (sr3anim's own README: "the obvious
// model is measurably wrong... it needs the runtime sampling subsystem, not
// more parsing") and no U4/U5 pose/skinning-per-frame pipeline either -
// inventing fake per-frame leg motion to make the rigid body LOOK like it
// is walking would be exactly the kind of fabrication this project's "no
// invented fixes" discipline forbids. What IS real and does respond to the
// walk: the character's WORLD position/orientation (driven by the real
// composer/integrator off the graph's own real T/E root-motion split and
// playback rate), not its on-screen silhouette.
//
// SCRIPTED INPUT (document the honest "turning" caveat plainly - this
// project's movement model has no separate turn-in-place stick; A and B are
// both translation axes read by PlayerMovement::readAxes()/moveSlot()):
//   frames  0..39 (40 frames): B = 1.0, A = 0.0  -> walk forward
//   frames 40..59 (20 frames): A = 0.6, B = 0.0  -> NOT a true turn in
//     place - engaging the A axis alone STRAFES the character sideways
//     while facingA50 (= unit(desiredMove)) swings to face the new
//     movement direction, so it reads as "turn + step", not a pure yaw.
//   frames 60..79 (20 frames): A = 0.0, B = 0.0  -> decelerate to a stop
// 80 frames total (the default --frames); default --capture-frame 79 (the
// last frame, fully stopped) so the captured frame shows the END state of
// a complete walk-turn-stop cycle, including however much the playback
// rate/locomotion state have settled by then.
//
// CHOSEN (every one distinct from the above, and from each other):
//   * the scripted axis sequence itself and the character's start position
//     (-960, 0, -700) - the SAME point tools/golden_scene_check.cpp's own
//     checkCityCornerScene() names "the box-overlap corner at focus", where
//     4 real hN cells meet; y=0 matches every streamed prop's own fake
//     local-grid Y placement in loadCell() below (there is no real ground-
//     height data anywhere in this scene - see runCity()'s own top comment -
//     so y=0 is the only internally-consistent choice, not an invented one);
//   * WalkRootMotion's constant 2.5 units/s for a looping (phase-
//     synchronised-proxy) clip, 0 for a one-shot clip - the SAME documented
//     stand-in bridge.h's own tests use (synthetic_player_anim_bridge_test.
//     cpp's Motion), since no real per-clip root motion exists anywhere in
//     this project (bridge.h's "WHAT T/E CANNOT BE");
//   * tagToRecord/duration on PlayerGraphServices - arbitrary bookkeeping
//     ids/constant duration, the SAME FakeServices precedent
//     tests/real_anim_node_graph_test.cpp already establishes against the
//     real 249-graph population (no real record/duration resolution
//     pipeline exists in this project either);
//   * the synthetic MoveTweaks values (maxRunSpeed/maxSprintSpeed/etc.) -
//     copied verbatim from tests/synthetic_player_movement_test.cpp's own
//     Rig / synthetic_player_anim_bridge_test.cpp's testMultiFrameGraphDrive
//     (test arithmetic, not recovered engine constants - no tweak_table.xtbl
//     load is done by this demo);
//   * MoveFrameInput.camera is WORLD-FIXED (row0 = +x, row2 = +z), the SAME
//     constant every existing sr3playeranim/sr3player test uses (frameAt()
//     in synthetic_player_anim_bridge_test.cpp / synthetic_player_movement_
//     test.cpp's Rig::frame()) - deliberately NOT tied to this viewer's own
//     free-fly debug camera, so the simulated walk is reproducible
//     regardless of where the debug eye happens to be pointed.
//
// NOT THIS COMMAND'S JOB (same scope boundary as sr3player/sr3animruntime/
// sr3playeranim themselves): real Havok physics, vehicles, combat/aim/
// weapons, NPC-specific anything, the real U4/U5 layer-and-pose system, the
// follow camera's own submode/collision pipeline when --v2 is NOT given
// (Judgement call 1 above; --v2 runs it for the on-foot common case only -
// vehicles/weapons/ragdoll/etc. remain out of scope either way, see V2 below).
//
// V2 (--v2 flag, 2026-10-07 follow-up task - fixes three real problems a
// review of the frozen tests/golden/walk_demo v1 baseline found; v1 itself
// is UNTOUCHED, byte-for-byte, when --v2 is not passed):
//
// FIX 1 - brad was an untextured flat Checker pattern because this command
// never resolved his real per-material diffuse textures at all (a "first
// cut" scope note on the old Checker draw call said so directly). --v2
// reuses runMesh()'s own --mode textured implementation VERBATIM (the same
// sr3geometry::MaterialBindings::parse()+findAndUploadTexture() per-material
// resolution, same drawTextured() call) against brad's own cb/geometry/mesh
// (already loaded identically to runPose() below) - NOT a new mechanism.
// (tests/golden/brad's own frozen golden, by contrast, is `pose`'s default
// Checker mode too - `pose` explicitly refuses --mode textured, see its own
// usage text, and defers to runMesh() for exactly this reason - so "brad's
// golden is already textured" was not the case; runMesh() is the real
// source this reuses.)
//
// FIX 2 - the capture used a hand-picked debug camera, not the follow
// camera that bridge.h's own file banner says this coupling exists to
// drive. --v2 constructs the SAME real sr3player::CameraSystem (still built
// unconditionally either way, Judgement call 1) but now: seeds its 61
// submodes + panning/aggression scalars from the REAL camera_free.xtbl
// (misc_tables.vpp_pc, the SAME archive already open for the node-graph
// tables - sr3player/camera_free_seed.h's existing reader, no new mechanism)
// for the on-foot common case (submode 0 "exterior close" only - the human
// chooser always returns it here, SeedFollowCameraOnFootCommonCase's own
// CHOSEN seeding); injects a NoHitCameraCollision (this project has no
// collision world - camera.h's own CHOSEN opt-in for exactly that case) and
// target.sizeFactor=1.0/noCollision=false so the real collision-resolve
// CODE PATH still runs (and genuinely reports "no hit" every frame, not
// skipped); calls the real cadence every frame - preUpdate(dt) before
// bridge.stepFrame() (angles before "physics", Sec4.3), DriveCameraTarget()
// then update(CameraFrameTimes{dt,dt,dt}) after it (position follows THIS
// frame's player position, Sec4.3's "Consequence" paragraph) - with dt the
// SAME fixed 1/30s animFrame already uses (deterministic; never the real
// wall-clock debug-camera dt). The capture's view-projection is built from
// followCam.committed().position/right/up/forward/fov (Sec4.10: "consumers
// read the committed set") via a new buildViewProjectionFromCamera() local
// helper (same right/up/forward->view-matrix construction buildFreeViewProjection
// already uses, just taking rows directly instead of deriving them from
// yaw/pitch) - NOT buildFreeViewProjection(eye,yaw,pitch,...) anymore.
// REAL, MEASURED, HONEST FINDING (not a bug, not fixed, stated plainly): the
// camera's effective HEADING does not auto-track brad's own turning during
// frames 40-59 (the 'turning' phase) - Sec4.6/Sec4.7 step 9/13 make heading
// a function of LOOK INPUT (panRateYaw/panRatePitch) integrated every
// pre-update, not of the target's facing; this demo's LookInputSource always
// returns 0 (no scripted look input, same as v1), so angles.heading stays
// at whatever followSnap() set it to at setup (brad's INITIAL heading) for
// the whole sequence - position tracks the live pivot (so the camera visibly
// translates with brad), orientation does not re-aim at him as he turns.
// This is the real spec-confirmed behaviour of a camera with no look input,
// not a shortcut taken here.
//
// FIX 3 - the STREAMED PROPS near brad (walls/pipes/buildings) rendered the
// wrong set because this command's own city streaming (loadCell()/
// reconcile(), copied verbatim from runCity()) keys cityWantedCells() off
// the FREE-FLY DEBUG CAMERA's `eye[]`, never brad's own simulated position -
// harmless in v1 only because v1's own frozen --eye happens to sit a few
// units from brad's start. --v2 keys reconcile() off brad's own live
// (pm.st.position.x, pm.st.position.z) every frame instead, so the loaded
// cell set follows wherever the scripted walk actually puts him (and, now
// that FIX 2 renders from the follow camera's own position rather than a
// chosen --eye, the two are no longer independently-chosen values that
// could disagree at all).
//
// FIX 4 (2026-10-07 follow-up task, GROUND SPECIFICALLY - do not conflate
// with FIX 3 above, which is about the hN PROP cells, e.g. walls/pipes/
// buildings, not the literal ground plane) - the GROUND itself (distinct
// from the props FIX 3 already fixed the streaming focus for) was still
// black/untextured under brad's feet even after FIX 3 landed. ROOT CAUSE,
// confirmed against the real spec, NOT the "city_corner has no literal
// ground-plane mesh" assumption a prior pass made and this task was
// specifically asked to re-investigate: spec-world-streaming.md Sec10.6(a)
// - the hN "Zone (High LOD)" fine cells this file streams for props hold
// ONLY `Level Mesh` entries (41,689/41,689, 0 exceptions) - never terrain.
// The real ground geometry for a tile lives in that tile's OWN "Zone"
// container (one level up), which additionally carries real "zone/zone-
// header/buffer" entries - i.e. the real `.czh_pc`/`.czn_pc`/`.gzn_pc`
// terrain trio (spec-terrain-format.md) - that this command never opened at
// all. That trio's real geometry is already decodable in this project
// (`sr3zone::ZoneGeometry::locate()` + `sr3mesh::MeshBlock`, the SAME
// pipeline this file's own `zone` command, runZoneTile() above, already
// uses against 2,971 real files) - a real, tested mechanism, not an
// unimplemented format. See CityGroundTile's/loadCityGroundTile()'s own
// "GROUND/TERRAIN" top comment (just above runCity(), shared by both `city`
// and `walk`) for the full account, including the honest "no real material/
// texture binding exists for zone data" dead end (MaterialIdAsColour, same
// as runZoneTile()). This applies to BOTH v1 and v2 (the ground/terrain
// reconciliation is added to the SAME loadCell()/reconcile() copy FIX 3
// already describes, at tile rather than cell granularity) - unlike FIX 1-3,
// it is NOT gated behind --v2, since it is a real geometry-completeness fix
// rather than a camera/texture/streaming-focus behavioural change.
//
// RE-CHECKED 2026-10-07, same follow-up task, after a same-day self-caught
// bug in the locator this paragraph originally relied on (see
// findTileZoneEntryName()'s own doc comment: its first pass only scanned an
// archive's immediate top-level entries, never recursing into nested
// `.str2_pc` containers the way findEntry() and every other real lookup in
// this project already do - now fixed to match). With that fix in place,
// v1's OWN three resident tiles (0916/1015/1016, confirmed via `sr3_viewer
// zone` - the same already-validated locate() pipeline tests/golden/
// zone_tile freezes) DO each have a real, substantial `.czn_pc` (268-407 KB)
// - but ZERO real Mesh sub-blocks are located in any of the three (`.gzn_pc`
// reads back 0 bytes for all three; no g-independent inline block exists
// either), so v1's captured PNG stays genuinely, verifiedly byte-identical -
// not by coincidence and not merely "checked" in the sense of reasoned
// about, but RE-RUN against the corrected binary and diffed. **What is NOT
// identical: v1's own stdout gains three new, purely informational
// "[ground] frame 0 tile NNNN: SKIPPED (no real ground rendered): ..."
// lines** (reconcile() now always attempts and logs the ground lookup for
// walk, same as it does for `city` when --ground is passed) - nothing is
// removed or reordered, only added, and tests/golden/walk_demo/
// baseline_stdout.txt needs exactly those 3 lines appended in place (see
// this task's own report for the exact diff) for checkWalkDemoScene()'s
// own stdout comparison to keep passing; the image comparison is
// unaffected either way. If a tile this command streams ever DOES carry
// real Mesh geometry, v1's PNG would genuinely gain visible ground then -
// this paragraph is why, and that would be a correct, wanted change, not a
// regression.
//
// CORRECTED 2026-10-08 (adversarial review follow-up): "ZERO real Mesh
// sub-blocks are located in any of the three" above is true of the BASE
// zone only, and was overstated as "the data genuinely has no ground here
// at all" - it does not, once the `~al` zone (nested in
// awld_compact.str2_pc, never examined by the pre-fix findTileZoneEntryName()
// - see that function's own "CORRECTED AGAIN 2026-10-08" paragraph) is
// actually looked at: each of 0916/1015/1016's `~al` zone carries 2 real
// Mesh sub-blocks (re-derived directly against the real archives this
// pass, same locate()/MeshBlock pipeline, independent of the reviewer's own
// numbers) - real, substantial, non-degenerate geometry, not a stub. The
// rendered PNGs still do not change (this pass still never draws `~al`
// geometry as ground - see findTileZoneEntryName()'s own "DECISION"
// paragraph for why, measured, not assumed: the `~al` blocks' own position
// bounding boxes run up to 116-310m across with Y (vertical) extents up to
// 116m, far too tall for a flat street-level surface - "rooftops/window-
// grid/scattered-patch" world-level LOD-0 content, consistent with what
// this file's own pre-existing top comment already said is deliberately
// not streamed at this LOD). What DOES change: the stdout `why` text for
// these 3 tiles now says so explicitly (`loadCityGroundTile()`'s own
// corrected message) instead of implying no real data exists anywhere for
// the tile - a wording/comment correction, not a rendering change, so v1's
// PNG stays byte-identical and only its SKIPPED line's wording differs.
// Whether `~al` geometry represents this location's real street-level
// ground remains an OPEN spec-sync question for Team A (see
// findTileZoneEntryName()'s own top comment) - deliberately not resolved
// here.
//
// --capture-frames F0,F1,F2,... (independent of --v2, but what v2's own
// golden uses it for): captures several frames in one run instead of one,
// each to '<--capture>_frameN.png' - so a short WALKING SEQUENCE is visible
// across several PNGs, not just one end-state frame. --capture-frame/
// --capture alone (the pre-existing flags, no --capture-frames) keep the
// original single-file behaviour byte-for-byte.
//
// Real-clip registry (walk --v2, 2026-10-07 U4 follow-up task): backs
// sr3animpose::ClipSource with a handful of REAL .anim_pc locomotion clips
// out of the real shipped preload_anim.vpp_pc, so the real U4 compose
// pipeline below has real keyframe data to drive instead of the old
// arbitrary-bookkeeping-id stand-in (bridge.h's "CHOSEN" judgement call -
// unchanged for any tag this registry does not recognise, see
// PlayerGraphServices wiring in runWalkDemo() below).
//
// WHICH CLIPS, AND WHY THESE: `plym_ps_run.anim_pc`, `plym_ps_jog.anim_pc`
// and `plym_ps_wlk.anim_pc` - found by this session's own probe
// (tools/match_anim_rig.cpp / tools/list_clean_clips.cpp against
// characters.vpp_pc's brad.rig_pc, 68 bones, and preload_anim.vpp_pc's
// clean population, spec-anim-format.md Sec6c.4/Sec10: flags bit 0x40
// clear, walk lands exactly on the file's own declared +0x30 offset). "ps"
// (pistol stance) is this archive's own generic player-locomotion family -
// the SAME prefix convention `cop_ps_*`/`plym_ps_*` share population-wide -
// and all three are 46-track clips, comfortably compatible with brad's
// 68-bone rig. This is NOT a claim that these are the production
// mapping's own real "run"/"jog"/"walk" clips for "player state machine"
// (that mapping is OPEN - this project has no anim_files.xtbl-backed clip
// registry anywhere, see sr3animpose/clip_pose_bridge.h's own ClipSource
// comment): it is this bridge's own CHOSEN real substitute, picked for
// being real, rig-compatible, and thematically correct, exactly the same
// spirit as WalkRootMotion's already-established synthetic-root-motion
// precedent just below.
class RealClipRegistry : public sr3animpose::ClipSource {
public:
    // Loads `clipName` from `archive` (expected already open on
    // preload_anim.vpp_pc), parsing its header (sr3anim::Animation) and
    // walking its keyframe payload (sr3anim::Payload) - the SAME two
    // calls tools/match_anim_rig.cpp's own walkClips() uses, UNMODIFIED.
    // Idempotent per name. Returns -1 (never throws) on a missing entry or
    // a parse/walk failure - the caller treats that exactly like any other
    // unresolved record (skipped, not fabricated; see ClipSource's own
    // doc comment).
    int32_t load(vpp::Container& archive, const std::string& clipName) {
        auto it = byName_.find(clipName);
        if (it != byName_.end()) return it->second;
        auto entry = std::make_unique<Entry>();
        std::vector<uint8_t> sibling;
        if (!findEntry(archive, clipName, entry->bytes, sibling, std::string())) {
            byName_[clipName] = -1;
            return -1;
        }
        try {
            vpp::ByteView bytes(entry->bytes.data(), entry->bytes.size());
            entry->animation = sr3anim::Animation::parse(bytes);
            entry->payload = sr3anim::Payload::walk(bytes, entry->animation);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "RealClipRegistry: %s failed to parse/walk: %s\n", clipName.c_str(), ex.what());
            byName_[clipName] = -1;
            return -1;
        }
        entry->resolved.animation = &entry->animation;
        entry->resolved.payload = &entry->payload;
        entry->resolved.bytes = vpp::ByteView(entry->bytes.data(), entry->bytes.size());
        const int32_t record = static_cast<int32_t>(entries_.size());
        entries_.push_back(std::move(entry));
        byName_[clipName] = record;
        return record;
    }

    const sr3animpose::ResolvedClip* lookup(int32_t record) override {
        if (record < 0 || static_cast<size_t>(record) >= entries_.size()) return nullptr;
        return &entries_[static_cast<size_t>(record)]->resolved;
    }

private:
    struct Entry {
        std::vector<uint8_t> bytes;
        sr3anim::Animation animation;
        sr3anim::Payload payload;
        sr3animpose::ResolvedClip resolved;  // views bytes/animation/payload above - fixed once load() succeeds
    };
    std::vector<std::unique_ptr<Entry>> entries_;
    std::map<std::string, int32_t> byName_;
};

void LogWalkDemoAnimDiagnostics(int frameNum, const sr3playeranim::PlayerAnimBridge& bridge,
                                const sr3animruntime::StateActionRegistry& stateActions,
                                const sr3animpose::PoseComposeResult* pose = nullptr,
                                const std::vector<sr3rig::Bone>* bones = nullptr);


// ===========================================================================
// REAL `.clmesh_pc` TRIANGLE COLLISION (2026-10-08 follow-up task) -
// replaces CityPropAabbCollisionWorld above as --collision's own world.
// ===========================================================================
//
// BACKGROUND (relayed from Team A this session, confirmed via their own
// disassembly, not re-derived here - this project's standing "Team B never
// disassembles" rule): the real engine's world collision for `.clmesh_pc`
// level pieces is a real triangle mesh (`hkpExtendedMeshShape`) wrapped by a
// pre-baked MOPP bounding-volume tree, one mesh SHAPE referenced (not
// copied) per level piece. This class does NOT parse any MOPP/
// hkpExtendedMeshShape binary structure - out of scope, per the
// orchestrator's own instruction - only the real TRIANGLE DATA has to be
// real; the acceleration structure over it is this project's OWN, CHOSEN
// (CollisionBvh above: an ordinary median-split binary BVH, leaf size 4).
//
// REAL DATA SOURCE: the SAME `.clmesh_pc`/`.glmesh_pc` render-group-0 Mesh
// sub-block loadCityProp() already decodes for rendering (sr3clmesh::
// LevelMesh::resolveReferencedMeshes() + sr3mesh::MeshBlock::parse(), the
// project's existing, tested `.clmesh_pc` pipeline - spec-physics-
// format.md Sec4.2/Sec4.4.6, spec-vertex-format.md) - channel vertex
// POSITIONS (decodeChannel(), NOT the block's own "primary"/index buffer -
// spec-zone-data-format.md Sec13.2's real methodology-bug correction: a
// Mesh sub-block's primary buffer is the INDEX buffer, real positions live
// in the separate per-channel array instead) expanded into real triangles
// via triangleListForRange() (strip-to-list, degenerates already dropped).
// loadCityProp()'s own `outCollisionTris` parameter (this task's own
// addition) captures exactly this, once, at load time; BuildCollisionBvh()
// above then translates it into world space (the SAME pure-translation
// transform CityPropAabbCollisionWorld's own doc comment above already
// confirms - this class relies on the identical fact) and builds the BVH.
//
// SCOPE OF WHAT IS REAL COLLISION HERE, stated plainly: `.clmesh_pc`'s own
// "Level Mesh" container type covers STATIC PROPS placed in a hN fine cell
// (walls, curbs, street furniture, small buildings pieces - see
// spec-zone-data-format.md Sec13.2's own population survey, which found
// `.clmesh_pc` in the same tiles it checked to be "street furniture only -
// 142 distinct props, zero road-named"), NOT the large city street/
// sidewalk/building-shell surface geometry (that is `.czn_pc`/`.gzn_pc`
// zone/terrain data, content-kind `0x2237`). UPDATE 2026-10-11 (street-
// collision-0x2237-mopp follow-up task): `0x2237`'s own real street-
// pavement VERTICAL support is now ALSO wired - see SampleGroundMeshHeight()
// above loadCityGroundTile() for the real `0x2237` collision-mesh ground-
// height sampler this task added, which supersedes the fake y<=groundY
// ground-plane binary (character_collision.h's own top comment) and the
// Heightmap (0x2244)-based fallback for runPlaytest()'s player/vehicle
// vertical support specifically. This class (ClmeshTriangleCollisionWorld)
// itself is UNCHANGED by that task - it still only ever queries `.clmesh_pc`
// prop triangles for its own HORIZONTAL sweepCylinder() (walls/curbs/street
// furniture); `0x2237`'s own pavement surface was never fed into THIS
// class's own horizontal wall-sweep query (a flat walkable floor has no
// business blocking horizontal movement the way a wall does) - only into
// the separate vertical ground-height sampler.
//
// ALSO UNCHANGED BY THIS TASK, same caveat CityPropAabbCollisionWorld's own
// doc comment above already states and runCity()'s own top comment first
// raised: a `.clmesh_pc` prop's WORLD POSITION inside its cell is a FAKE
// local grid slot, not the game's real per-instance placement (that
// mechanism is NOT resolved by this project - see runClmeshTile()'s own
// "REAL PER-INSTANCE PLACEMENT WAS INVESTIGATED FIRST, AND DID NOT
// RESOLVE" top comment for the full investigation). So this class gives
// Brad REAL triangle SHAPES to collide against, at the SAME
// already-fake grid positions the former AABB stand-in used - a strictly
// better approximation of "what shape blocks movement", not a fix for
// "where that shape actually sits in the real game world". Both honestly
// labelled here rather than silently improved past what was actually done.
class ClmeshTriangleCollisionWorld : public sr3player::ICollisionWorld {
public:
    explicit ClmeshTriangleCollisionWorld(const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded)
        : loaded_(loaded) {}

    bool sweepCylinder(const sr3player::Vec3& from, const sr3player::Vec3& to, double radius, double groundY,
                       double height, sr3player::WallContact& out) const override {
        double bestF = 1.0;
        sr3player::Vec3 bestNormal{};
        bool found = false;
        uint32_t bestSurfaceId = 0;
        for (const auto& kv : loaded_) {
            const CityLoadedCell& lc = *kv.second;
            for (size_t pi = 0; pi < lc.props.size(); ++pi) {
                const CityProp& p = lc.props[pi];
                if (p.collisionBvh.empty()) continue;  // no real triangles captured for this prop (--collision
                                                         // only builds this; see CityProp's own doc comment)
                const uint32_t surfaceBase = static_cast<uint32_t>(kv.first * 1000000u + pi * 10000u);
                sweepBvhNode(p.collisionBvh, p.collisionBvh.root, from.x, from.z, to.x, to.z, radius, groundY, height,
                            surfaceBase, bestF, bestNormal, bestSurfaceId, found);
            }
        }
        if (!found) return false;
        out.travelFraction = bestF;
        out.normal = bestNormal;
        out.point = sr3player::Vec3{from.x + (to.x - from.x) * bestF, from.y, from.z + (to.z - from.z) * bestF};
        out.surfaceId = bestSurfaceId;
        return true;
    }

private:
    // Recursive BVH descent: reject a whole subtree by its own (Y-gated,
    // radius-expanded) AABB against the swept segment's own bounding box
    // before ever touching a real triangle - the same two-stage broad/
    // narrow-phase shape CityPropAabbCollisionWorld already used at the
    // PROP level (Y-overlap, then XZ), just one level deeper, per-triangle,
    // inside each prop's own BVH.
    static void sweepBvhNode(const CollisionBvh& bvh, int nodeIdx, double x0, double z0, double x1, double z1,
                             double radius, double groundY, double height, uint32_t surfaceBase, double& bestF,
                             sr3player::Vec3& bestNormal, uint32_t& bestSurfaceId, bool& found) {
        if (nodeIdx < 0) return;
        const CollisionBvhNode& node = bvh.nodes[static_cast<size_t>(nodeIdx)];
        if (static_cast<double>(node.max[1]) < groundY || static_cast<double>(node.min[1]) > groundY + height)
            return;  // whole subtree never reaches the capsule's vertical band
        const double nMinX = static_cast<double>(node.min[0]) - radius;
        const double nMaxX = static_cast<double>(node.max[0]) + radius;
        const double nMinZ = static_cast<double>(node.min[2]) - radius;
        const double nMaxZ = static_cast<double>(node.max[2]) + radius;
        const double pMinX = (std::min)(x0, x1), pMaxX = (std::max)(x0, x1);
        const double pMinZ = (std::min)(z0, z1), pMaxZ = (std::max)(z0, z1);
        if (pMaxX < nMinX || pMinX > nMaxX || pMaxZ < nMinZ || pMinZ > nMaxZ) return;  // swept path never
                                                                                        // reaches this subtree
        if (node.left < 0) {  // leaf
            for (int i = node.triStart; i < node.triStart + node.triCount; ++i) {
                const CollisionTriangle& t = bvh.tris[static_cast<size_t>(i)];
                const float triMinY = (std::min)({t.v0[1], t.v1[1], t.v2[1]});
                const float triMaxY = (std::max)({t.v0[1], t.v1[1], t.v2[1]});
                if (static_cast<double>(triMaxY) < groundY || static_cast<double>(triMinY) > groundY + height)
                    continue;
                double f;
                sr3player::Vec3 n;
                if (sr3player::SweepCircleVsTriangleXZ(x0, z0, x1, z1, radius, t.v0[0], t.v0[2], t.v1[0], t.v1[2],
                                                        t.v2[0], t.v2[2], f, n) &&
                    f < bestF) {
                    bestF = f;
                    bestNormal = n;
                    bestSurfaceId = surfaceBase + static_cast<uint32_t>(i);
                    found = true;
                }
            }
            return;
        }
        sweepBvhNode(bvh, node.left, x0, z0, x1, z1, radius, groundY, height, surfaceBase, bestF, bestNormal,
                    bestSurfaceId, found);
        sweepBvhNode(bvh, node.right, x0, z0, x1, z1, radius, groundY, height, surfaceBase, bestF, bestNormal,
                    bestSurfaceId, found);
    }

    const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded_;
};


// ---- real camera collision (2026-10-08 castRay/shape-cast follow-up
// task) - spec-physics-runtime.md Sec8.8/Sec8.11: camera collision is
// CONFIRMED to go through the shared SHAPE-cast wrapper 0x0075c880 (an
// hkpSphereShape swept cast, "first blocking hit" reduction over gathered
// results - NOT a single-closest collector; spec-player-runtime.md
// Sec4.9's own 0x00565d70 is the game-side caller). Supersedes
// sr3player::NoHitCameraCollision (the prior "no collision world in this
// project" CHOSEN opt-in, camera.h's own doc comment) the SAME way
// ClmeshTriangleCollisionWorld above superseded CityPropAabbCollisionWorld
// for the character proxy - real geometry in place of an explicit stand-in,
// wired ONLY when --collision is passed (see runWalkDemo()'s own wiring
// below; without --collision this command's camera behaviour, including
// both frozen walk_demo_v2 goldens, is BYTE-FOR-BYTE UNCHANGED).
//
// REAL DATA SOURCE, REUSED NOT RE-DECODED: the SAME per-prop
// CollisionBvh/CollisionTriangle data ClmeshTriangleCollisionWorld already
// queries above - one real `.clmesh_pc` triangle source, two query SHAPES
// (a 2D XZ circle-sweep for the character capsule, a genuine 3D swept
// sphere here for the camera) via sr3player::SweptSphereVsTriangle3D
// (include/sr3player/character_collision.h, this task's own addition -
// the "shared, reusable" 3D primitive the orchestrator's brief asked for).
//
// SCOPE, same honest caveat as ClmeshTriangleCollisionWorld above: static
// PROPS only (walls, curbs, street furniture) in resident cells - NOT the
// .czn_pc/.gzn_pc city street/building-shell surface, and props still sit
// at a FAKE per-cell grid position (both unchanged by this task). ALSO
// HONEST: there is no character or vehicle mesh anywhere in this triangle
// soup (walk_demo renders Brad as a rigid body with no collision shape of
// his own, and has no vehicle at all) - spec-player-runtime.md Sec4.9's
// own camera filter 0x00566070 ("skips hits on the local player's own
// vehicle of kinds 1, 4, 8") and spec-physics-runtime.md Sec8.9's
// query-local caller-mode-byte filter both exist to exclude exactly that
// kind of object from blocking the camera - with none of that in this
// project's own real geometry source, there is nothing for either filter
// to ever exclude, so sr3player::CameraCollision::sweepSphere() is not
// extended with an unused filter-mode-byte parameter here. Reported as a
// deliberate, honest divergence (nothing to filter, not a silent match),
// not a claim that the real filter mechanism was reproduced.
class RealCameraCollision : public sr3player::CameraCollision {
public:
    explicit RealCameraCollision(const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded)
        : loaded_(loaded) {}

    std::optional<sr3player::CameraSweepHit> sweepSphere(sr3player::Vec3 from, sr3player::Vec3 to,
                                                          double radius) override {
        double bestF = 1.0;
        sr3player::Vec3 bestNormal{};
        bool found = false;
        for (const auto& kv : loaded_) {
            const CityLoadedCell& lc = *kv.second;
            for (const CityProp& p : lc.props) {
                if (p.collisionBvh.empty()) continue;
                sweepBvhNode3D(p.collisionBvh, p.collisionBvh.root, from, to, radius, bestF, bestNormal, found);
            }
        }
        if (!found) return std::nullopt;
        sr3player::CameraSweepHit hit;
        hit.position = sr3player::Vec3{from.x + (to.x - from.x) * bestF, from.y + (to.y - from.y) * bestF,
                                        from.z + (to.z - from.z) * bestF};
        hit.normal = bestNormal;
        // Same diagnostic shape as the character proxy's own "[collision]
        // frame-local wall hit" print (runWalkDemo()'s collisionHook lambda
        // above) - lets a --collision run's own stdout directly confirm the
        // real swept-sphere camera query actually fired, not just that it
        // compiled.
        std::printf("[camera-collision] frame-local camera hit at (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) f=%.4f\n",
                    hit.position.x, hit.position.y, hit.position.z, hit.normal.x, hit.normal.y, hit.normal.z, bestF);
        return hit;
    }

private:
    // Same two-stage broad/narrow-phase shape as ClmeshTriangleCollisionWorld's
    // own sweepBvhNode() above, one level deeper (full 3D here instead of
    // Y-gated-XZ - the camera moves in genuine 3D, not just horizontally,
    // so all three axes need the broad AABB-vs-swept-segment reject, not
    // just two of them).
    static void sweepBvhNode3D(const CollisionBvh& bvh, int nodeIdx, const sr3player::Vec3& from,
                               const sr3player::Vec3& to, double radius, double& bestF,
                               sr3player::Vec3& bestNormal, bool& found) {
        if (nodeIdx < 0) return;
        const CollisionBvhNode& node = bvh.nodes[static_cast<size_t>(nodeIdx)];
        const double nMinX = static_cast<double>(node.min[0]) - radius, nMaxX = static_cast<double>(node.max[0]) + radius;
        const double nMinY = static_cast<double>(node.min[1]) - radius, nMaxY = static_cast<double>(node.max[1]) + radius;
        const double nMinZ = static_cast<double>(node.min[2]) - radius, nMaxZ = static_cast<double>(node.max[2]) + radius;
        const double pMinX = (std::min)(from.x, to.x), pMaxX = (std::max)(from.x, to.x);
        const double pMinY = (std::min)(from.y, to.y), pMaxY = (std::max)(from.y, to.y);
        const double pMinZ = (std::min)(from.z, to.z), pMaxZ = (std::max)(from.z, to.z);
        if (pMaxX < nMinX || pMinX > nMaxX || pMaxY < nMinY || pMinY > nMaxY || pMaxZ < nMinZ || pMinZ > nMaxZ)
            return;  // swept path never reaches this subtree
        if (node.left < 0) {  // leaf
            for (int i = node.triStart; i < node.triStart + node.triCount; ++i) {
                const CollisionTriangle& t = bvh.tris[static_cast<size_t>(i)];
                double f;
                sr3player::Vec3 n;
                if (sr3player::SweptSphereVsTriangle3D(from.x, from.y, from.z, to.x, to.y, to.z, radius, t.v0[0],
                                                        t.v0[1], t.v0[2], t.v1[0], t.v1[1], t.v1[2], t.v2[0],
                                                        t.v2[1], t.v2[2], f, n) &&
                    f < bestF) {
                    bestF = f;
                    bestNormal = n;
                    found = true;
                }
            }
            return;
        }
        sweepBvhNode3D(bvh, node.left, from, to, radius, bestF, bestNormal, found);
        sweepBvhNode3D(bvh, node.right, from, to, radius, bestF, bestNormal, found);
    }

    const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded_;
};

// NPC line-of-sight over the same real `.clmesh_pc` prop collision the character/camera worlds use (AI-3). A zero-radius
// closest-hit ray (sr3player::RayVsTriangle3D, the primitive sr3airuntime::ClosestHitRayVsTriangles also uses) from the
// first to the second point through every loaded prop's CollisionBvh. clear() is true when nothing lies between them.
// Static props only: street/building-shell geometry that is not a `.clmesh_pc` prop does not block (same coverage as the
// character collision world), so a clear answer is "no known prop in the way", not proof of visibility.
class RealPropLineOfSight {
public:
    explicit RealPropLineOfSight(const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded) : loaded_(loaded) {}

    bool clear(double x0, double y0, double z0, double x1, double y1, double z1) const {
        for (const auto& kv : loaded_) {
            const CityLoadedCell& lc = *kv.second;
            for (const CityProp& p : lc.props) {
                if (p.collisionBvh.empty()) continue;
                if (rayBlockedNode(p.collisionBvh, p.collisionBvh.root, x0, y0, z0, x1, y1, z1)) return false;
            }
        }
        return true;
    }

private:
    static bool rayBlockedNode(const CollisionBvh& bvh, int nodeIdx, double x0, double y0, double z0, double x1,
                               double y1, double z1) {
        if (nodeIdx < 0) return false;
        const CollisionBvhNode& node = bvh.nodes[static_cast<size_t>(nodeIdx)];
        if ((std::max)(x0, x1) < node.min[0] || (std::min)(x0, x1) > node.max[0] || (std::max)(y0, y1) < node.min[1] ||
            (std::min)(y0, y1) > node.max[1] || (std::max)(z0, z1) < node.min[2] || (std::min)(z0, z1) > node.max[2])
            return false;
        if (node.left < 0) {
            for (int i = node.triStart; i < node.triStart + node.triCount; ++i) {
                const CollisionTriangle& t = bvh.tris[static_cast<size_t>(i)];
                double f;
                sr3player::Vec3 n;
                if (sr3player::RayVsTriangle3D(x0, y0, z0, x1, y1, z1, t.v0[0], t.v0[1], t.v0[2], t.v1[0], t.v1[1], t.v1[2],
                                                t.v2[0], t.v2[1], t.v2[2], f, n))
                    return true;
            }
            return false;
        }
        return rayBlockedNode(bvh, node.left, x0, y0, z0, x1, y1, z1) ||
               rayBlockedNode(bvh, node.right, x0, y0, z0, x1, y1, z1);
    }

    const std::map<size_t, std::unique_ptr<CityLoadedCell>>& loaded_;
};

} // namespace sr3viewer
