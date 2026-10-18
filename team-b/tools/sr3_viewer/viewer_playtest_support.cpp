#include "viewer_platform.h"
#include "viewer_playtest_support.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3airuntime/ped_navigator.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/road_graph_placement.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3zone/zone_heightmap.h"
#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/police_roadblock_spawn.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/auto_combat.h"
#include "vpp/container.h"
#include "sr3audio/hirc.h"
#include "sr3audio/media_bank.h"
#include "sr3audioruntime/audio_output_device.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3tables_environment/tables.h"
#include "sr3tables_cutscene/scene_table.h"
#include "sr3xtbl/xtbl.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3radio/radio_player.h"

using namespace sr3viewer;

namespace {

// ===========================================================================
// `playtest` (2026-10-08, "playtest v0" milestone) - the first genuinely
// INTERACTIVE build: a real human plays it with keyboard+mouse, in a real
// VISIBLE window, instead of watching a scripted/deterministic capture.
//
// This is runWalkDemo()'s own real pipeline (brad's real rigged+textured
// mesh, the real 'player state machine' node graph via
// sr3playeranim::PlayerAnimBridge, the real sr3animpose U4 pose-compose
// library, the real follow camera sr3player::CameraSystem, the real
// ClmeshTriangleCollisionWorld/CharacterProxyDriver character-proxy
// collision, the real city_corner ground/prop streaming) with exactly THREE
// substitutions, each called out below where it happens:
//   1. WalkScriptSource/FixedLookSource's SCRIPTED A/B and always-zero
//      look input are replaced by PlaytestMoveSource/PlaytestLookSource,
//      which poll REAL GetAsyncKeyState (WASD + Shift, the SAME polling
//      shape every fly-camera command in this file already uses - see
//      runScene()'s own per-frame block) and REAL mouse deltas, unless
//      --no-input is passed (ctest/automated use), in which case a
//      frame-scheduled scripted fallback drives both - the SAME
//      live-vs-scripted split runWalkDemo()'s own --no-input already
//      establishes for its debug fly-camera, just applied to the
//      character/look input seam instead.
//   2. runWalkDemo()'s own "WORLD-FIXED, not the debug camera" camera rows
//      (a CHOSEN simplification that command's own doc comment names
//      explicitly) are replaced here by the REAL camera rows
//      (CameraSystem::movementRows(), spec-player-runtime.md Sec3.5.5's
//      R0/R2 "the camera orientation written by the camera update that ran
//      earlier in the same world update") - so WASD here genuinely moves
//      relative to wherever the mouse is currently looking, the real
//      Sec4.3 cadence (cameraHook -> preUpdate -> move slot reads
//      movementRows() -> physics -> update), not a fixed world axis.
//   3. The spawn point's ground height is MEASURED, not assumed: this
//      command loads the real ground tiles at the chosen spawn corner
//      BEFORE warping brad there, reads MeshRenderer::boundsMin()/
//      boundsMax() (already-public accessors, no reader change) off the
//      REAL decoded `~s` ground geometry (findTileZoneEntryName()'s
//      tilde-s fallback, already merged to main), and only then picks
//      brad's spawn Y and the character-proxy's groundY plane from that
//      real measurement - see "GROUND-Y MEASUREMENT" below.
//
// NOT carried over from runWalkDemo(): --tint, the focus-window debug
// outline draw (focusBox is still created, only for its shared depth
// buffer - never drawn), --head-matlib-attach/--head-matlib-bind (CHOSEN,
// not spec-confirmed heuristics - out of scope for a first playable build),
// --walk-forward-frames/--walk-turn-frames naming (renamed --script-*-frames
// here, same idea, since there is no live walk to override without
// --no-input).
//
// UPDATED 2026-10-08 (merged main in, late in this same task, per this
// task's own "check before finishing" instruction - `git log`/`git branch`
// re-checked directly, not assumed): two of the three features this
// function's own first draft called "not yet merged" landed on main WHILE
// this task was in progress and are now wired in:
//   * GROUND TEXTURES: main's own ground-tile-texture-resolution fix
//     (CityGroundTile::perMaterialSrv, resolveGroundTextureBindings())
//     landed; this function's ground-tile draw call below now calls
//     drawTextured() when it resolved real textures, falling back to the
//     original MaterialIdAsColour flat colour (byte-identical to before)
//     only when it did not - never a regression, same safe-empty-vector
//     guard runWalkDemo()'s own copy of this draw call uses.
//   * LIVE HUD: main's own `hud-live-demo` command (health/ammo/money/
//     notoriety, spec-hud-runtime.md, driven by REAL Lua natives through a
//     real sr3luahost::Host/EngineState, rendered via this project's real
//     VINT text pipeline/VintQuadPipeline) landed too. --hud <registration_
//     list.txt> <misc.vpp_pc> wires the SAME overlay in here, composited
//     onto this command's own live swapchain render target every frame
//     (VintQuadPipeline::begin()/quad()/rect() never touch OMSetRenderTargets
//     themselves, so they draw straight on top of whatever this function's
//     own 3D pass already bound - confirmed by reading the class, not
//     assumed). HONEST CAVEAT: v0 has no combat/economy/notoriety GAMEPLAY
//     wired (no weapons, no damage, no cash pickups) - the overlay reads
//     REAL Lua-backed state through the REAL natives (not hardcoded
//     numbers), but since nothing in THIS command currently mutates that
//     state during play, it will show an unchanging full-health/no-ammo/
//     $0/notoriety-0 readout, not a live-changing one. That is a true
//     statement about this build's own scope, not a bug in the overlay
//     plumbing itself. OFF by default (an explicit opt-in stretch goal,
//     per this task's own "each behind its own toggle" instruction).
//   * This function was relocated (moved, not rewritten) from just after
//     runWalkDemo() to just before main() so it can see VintQuadPipeline/
//     HudDemoFont/drawHudLabel, which the hud-live-demo merge defines
//     textually later in this file - same translation unit either way.
//
// UPDATED 2026-10-08 (playtest-vehicle task): vehicle entry/driving IS now
// wired in, ON by default (--no-vehicle opts out). The car-parts wheel-
// placement fix this doc comment used to say was blocking it (commit
// e225cdd + its own follow-up fix) is merged to main as of this task's own
// start - re-checked directly, not assumed. One real vehicle (the same
// car_4dr_genki_0.ccar_pc :: dlc1.vpp_pc pairing runDriveDemo() uses,
// derived from <characters.vpp_pc>'s own directory - see --vehicle-archive/
// --vehicle-name below to override) is parked a few metres from the spawn
// point; walking within range shows a console "press E to enter" prompt
// (plus an on-screen HUD label when --hud is also given); E toggles
// between walking and driving. See PlaytestVehicleRig (just above
// runDriveDemo(), this file) for the real mesh-load/wheel-pose/paint path
// this reuses, and the "VEHICLE ENTRY/EXIT" block inside this function's
// own main loop below for the state machine. HONEST SCOPE for THIS
// integration specifically: the driving camera is a simple CHOSEN chase cam
// (runDriveDemo()'s own simplified trailing-camera formula, not the real
// CameraSystem's vehicle submode - spec does not cover one), and only ONE
// vehicle ever exists (no traffic/spawn system - still correctly OUT of v0,
// unchanged).
//
// UPDATED 2026-10-08 (vehicle-world-collision follow-up task, spec-physics-
// runtime.md §10a): the vehicle now HAS real world collision, superseding
// this doc comment's own former "the vehicle has no world collision (same as
// runDriveDemo() - sr3physics::VehicleSubstepSimulator is a planar, self-
// contained model with no tie to ClmeshTriangleCollisionWorld)" line - that
// was true when first written (this is the orchestrator's own observed "car
// drives through a street-light pole" gap) and is now fixed, here only
// (runDriveDemo() itself is untouched - still the original planar-only
// model, its own frozen drive_demo_coverage golden unaffected). See
// PlaytestVehicleRig::Advance()/ApplyWorldCollisionResponse()'s own doc
// comments for the full account: a bounding-sphere approximation of the
// vehicle's footprint is swept each frame against the SAME real `.clmesh_pc`
// triangles/BVH the player's own CharacterProxyDriver and the follow
// camera's own RealCameraCollision already query (reused via
// RealCameraCollision::sweepSphere(), not a second triangle decode), with a
// plain velocity-clamp impulse response (§10a: "no separate Volition-
// authored impulse/restitution formula... Havok's own solver does it
// directly") applied immediately each frame rather than through the real
// engine's deferred multi-threaded contact queue - a CHOSEN, stated
// simplification (this harness's own loop is single-threaded and
// synchronous, so there is no second thread/frame boundary to defer across).
// Vehicle-vs-vehicle collision and vehicle damage/HP are explicitly OUT of
// scope for this follow-up (only ONE vehicle ever exists here anyway, so
// there is nothing to collide with yet; damage/HP is spec-confirmed to be a
// separate, larger, not-yet-dispatched unit, §10a's own "genuinely OPEN"
// damage-amount formula).
//
// UPDATED 2026-10-09 (playtest-nearfield-traffic-roadblock task) - the
// world is no longer empty. TWO independent, previously-standalone systems
// are now wired into this function's own live loop (same single-owner
// pass, bundled together per that task's own brief so only one agent
// touches this file this round):
//
//   * NEAR-FIELD (fully-simulated) AMBIENT VEHICLE TRAFFIC
//     (include/sr3airuntime/near_field_vehicle_population.h, spec-ai-
//     runtime.md Sec17.4c) - ALWAYS ON, not behind a flag (ordinary
//     background traffic, matching how a real player experiences the
//     city). NearFieldVehicleDriver::ShouldRunMaintenancePass()/
//     ShouldRunDespawnSchedule() tick every frame from nowMsAccum (the SAME
//     accumulated clock the TOD system already advances), fed the LIVE
//     focusX/finalGroundY/focusZ position (the real player on foot or the
//     real vehicle while driving - never a synthetic script, even under
//     --no-input). CHOSEN (none of this contradicts anything Sec17.4c
//     itself states - see that header's own CONFIRMED/CHOSEN/OPEN table
//     for the underlying algorithm, unmodified here):
//       - spawn candidates: GatherNearFieldSpawnCandidatesAroundPlayer()
//         (just above this function) samples an annulus around the live
//         reference position, biased toward its current heading, rather
//         than a real road-graph walk - this project's playtest city
//         loader has no Traffic/Sidewalks `.czn_pc` content-kind reader
//         wired in (same honest gap this doc comment's own "ambient
//         pedestrians" note below already states for the distant/LOD ped
//         tier);
//       - zoneDataLoaded is passed a constant `true` (no real Traffic
//         zone-data singleton exists in this build to query);
//       - eligibleHumanCount is passed a constant 0 (no live ambient-human
//         population/census exists in this build either, so the shared
//         150-human headroom gate is always satisfied, never fabricated);
//       - DRAWING reuses the SAME already-loaded vehicleRig (the player's
//         own parked/driven car body+wheel mesh) at each live near-field
//         vehicle's own position, rather than loading up to 48 independent
//         .ccar_pc meshes - every ambient vehicle currently looks like the
//         same car model, an honest, stated simplification, not a bug.
//         Skipped when --no-vehicle leaves nothing to reuse (the
//         population/despawn bookkeeping itself still runs regardless).
//
//   * POLICE ROADBLOCK SPAWN (include/sr3airuntime/police_roadblock_spawn.h,
//     spec-ai-runtime.md Sec19.7) - OFF by default, toggled by a new debug
//     key, 'R' (edge-triggered, same prevEDown/vehEEdge debounce shape
//     vehicle entry already uses). THE HONEST NOTORIETY GATE (see that
//     header's own "THE HONEST NOTORIETY GATE" section): RoadblockPopulation
//     Driver::armed is NOT wired to any notoriety threshold - that upstream
//     link is spec-confirmed OPEN (Sec19.5/19.6/19.7's own "no function in
//     this chain reads the live notoriety record" finding), so inventing a
//     notoriety threshold here would fabricate exactly the fact Team A
//     spent that pass confirming does NOT exist. 'R' is therefore a plain,
//     labelled TEST/DEBUG gate - exactly how police_roadblock_demo.cpp's own
//     console demo already exercises it (driver.armed = true there), just
//     reachable live instead of only via a scripted demo. While armed, the
//     real throttle/anchor/dispatch chain (RoadblockPopulationDriver::
//     RunTick(), unmodified) ticks every frame anchored at the SAME live
//     focusX/finalGroundY/focusZ reference position near-field traffic
//     uses. DRAWING: roadblock peds reuse the SAME already-loaded/posed
//     brad character mesh (characterRenderer/characterHeadRenderer) at each
//     filled ped-category slot's own resolved position (regardless of
//     inVehicle - a roadblock ped stays visible whether the player is
//     walking or driving past); roadblock vehicles reuse vehicleRig, same
//     as near-field traffic above. Case 4 (ThirdCategoryCreationRequest,
//     real-world identity OPEN per that header's own CHOSEN note) has no
//     mesh wired up - position bookkeeping only, honestly not drawn rather
//     than guessing a shape. A small per-slot cache (roadblockSlotVisual[],
//     declared just above the main loop) remembers each filled slot's own
//     resolved creation request across frames (the header's own
//     RoadblockSlot does not itself store one) by diffing `filled` before/
//     after each tick against the manager's own public slots() - see that
//     declaration's own comment for the one documented edge case this
//     diffing does not perfectly cover (a slot that both frees AND is
//     immediately refilled within the SAME tick keeps its OLD cached
//     visual one tick stale - a cosmetic rendering-glue limitation, not a
//     population-logic bug; harmless given the 45s occupant lifetime).
//
// UPDATED 2026-10-10 (near-field-peds-playtest task) - the STALE note
// directly above this one (said here once, since the ambient-population
// distinction is genuinely easy to conflate) no longer describes current
// status for the NEAR-FIELD ped tier. Two entirely separate pedestrian
// systems exist in this codebase (ambient_population.h's own file header
// draws the same line near_field_ped_population.h's file header draws):
//   * The DISTANT/LOD human tier (ambient_population.h's own ped side,
//     spec-ai-runtime.md Sec17.3/17.6) - STILL out of v0, unrelated to this
//     task, still no spec-confirmed pass wiring it into playtest; the
//     roadblock's OWN ped category (case 2, above) is a separate,
//     debug-gated mechanism, not that system either.
//   * The NEAR-FIELD (fully-simulated) ambient PEDESTRIAN tier
//     (include/sr3airuntime/near_field_ped_population.h, spec-ai-runtime.md
//     Sec20) - NOW WIRED IN by this task, following the near-field
//     VEHICLE traffic wiring above as its exact precedent: ALWAYS ON (not
//     behind a flag, same "ordinary background pedestrians, matching how a
//     real player experiences the city" reasoning the vehicle side's own
//     doc comment states), ticked every frame from the SAME nowMsAccum
//     clock via NearFieldPedDriver::ShouldRunTick(), fed the SAME live
//     focusX/finalGroundY/focusZ reference position and heading. CHOSEN
//     (same honest-gap discipline as the vehicle side):
//       - candidate sidewalk segments: GatherNearFieldPedSpawnCandidate
//         SegmentsAroundPlayer() (near GatherNearFieldSpawnCandidates
//         AroundPlayer(), just above this function) synthesizes a small
//         set of short segments scattered in a heading-biased annulus
//         around the player (same reasoning the vehicle side's annulus
//         uses, a DIFFERENT, farther-out radius band - see that function's
//         own comment for why: the ped spawn algorithm's own ~90m
//         camera-near-reject gate would otherwise discard almost every
//         candidate the vehicle band's own 25-90m range produces), rather
//         than a real sidewalk-graph walk - this project's playtest city
//         loader still has no Sidewalks `.czn_pc` content-kind reader wired
//         in (the identical gap the vehicle side's own doc comment already
//         states, now restated for the ped side rather than left stale);
//       - sidewalksLoaded is passed a constant `true` (no real Sidewalks
//         zone-data singleton exists in this build to query, mirroring
//         zoneDataLoaded=true on the vehicle side);
//       - eligibleHumanCount is passed the SAME constant 0 the vehicle
//         side already passes (shared 150-human headroom gate, reused by
//         #include - no live ambient-human census exists in this build);
//       - localCap: near_field_ped_population.h's own file header states
//         this "local headroom" has no CONFIRMED numeric literal (unlike
//         the vehicle tier's CONFIRMED 48) and is CALLER-SUPPLIED. CHOSEN
//         here: reuse the one known near-field-population cap precedent,
//         kNearFieldVehicleCap (48) - an honestly-labelled CHOSEN stand-in,
//         not a second confirmed literal;
//       - DRAWING reuses the SAME already-loaded/posed brad character mesh
//         (characterRenderer/characterHeadRenderer) the police-roadblock
//         ped category already draws with, at each live near-field ped's
//         own population position - every ambient pedestrian currently
//         looks like the same character model, the identical honest,
//         stated simplification the vehicle side's own "every ambient
//         vehicle currently looks like the same car model" note applies.
//     This module's own SPAWN-ONLY scope (no despawn predicate exists in
//     spec-ai-runtime.md Sec20 for near-field peds - see that header's own
//     file note) means nearFieldPedsLive only ever grows up to localCap
//     within a single playtest run; not a bug, an honestly-scoped gap this
//     wiring does not paper over.
//
// HONEST SCOPE (said here once, not scattered): keyboard+mouse only. This
// project's own spec-tables-ui-controls.md Sec3-Sec5 DOES document a real
// gamepad/XInput binding layer (control_schemes.xtbl, the 19-entry
// gamepad-button table) - it is not wired here. No jump, no crouch toggle
// (PlaytestMoveSource::held() only ever answers Sprint). The DISTANT/LOD
// ambient PEDESTRIAN population remains out of scope (see above); near-field
// ambient VEHICLE traffic, near-field ambient PEDESTRIAN population, and the
// debug-gated police roadblock ARE now wired (see above).
//
// UPDATED 2026-10-08 (sky/lighting/TOD task): the fixed CHOSEN skyblue clear
// colour and mesh_renderer.cpp's fixed CHOSEN "headlight" term (both from an
// earlier, separate task) are REPLACED by a real per-frame weather/time-of-
// day blend now that spec-tables-environment.md Sec18 confirms the runtime
// mechanism (state machine + 2x2 bilinear TOD-segment x weather-stage corner
// blend). A CHOSEN clock (todFraction, declared just above this function's
// own frame loop) advances once per frame - a full in-game day every 20
// REAL seconds, starting at noon - and feeds sr3render::ComputeTodLighting()
// (include/sr3render/tod_lighting.h), which reads the real
// weather_time_of_day.xtbl (loaded from <misc_tables.vpp_pc>, optional - see
// the "weather_time_of_day.xtbl" block below) for a single CHOSEN constant
// weather-stage name ("Light Rain" - see that block's own comment for why).
// Sky/clear colour, ambient colour and sun colour all now vary per frame;
// sun DIRECTION also varies (a CHOSEN cosmetic day-arc - Sec18 confirms NO
// real sun-direction field exists anywhere in this table group). See
// sr3render/tod_lighting.h's own banner for the full CONFIRMED-vs-CHOSEN
// breakdown, including why the weather-STAGE axis of the real blend is not
// simulated by a live schedule here (a separate, substantially larger
// task). Every other command in this file (runWalkDemo/runDriveDemo/
// runCityDemo/golden_scene_check/...) is UNCHANGED - this is scoped to
// runPlaytest()'s own per-frame values, passed through mesh_renderer.cpp's
// new, purely-additive optional sunDirection/ambientColor/sunColor
// parameters (null everywhere else, reproducing the old fixed values byte-
// for-byte - see MeshRenderer::draw()'s own comment).
// GatherNearFieldSpawnCandidatesAroundPlayer (THIS TASK,
// playtest-nearfield-traffic-roadblock, 2026-10-09) - the CHOSEN
// spawn-candidate SOURCE runPlaytest() feeds SelectNearFieldVehicleSpawn()'s
// own 3-slot buffer (near_field_vehicle_population.h's own file header,
// CHOSEN item 1: "callers supply up to kNearFieldSpawnCandidateSlots (3)
// pre-filtered candidate positions"). tools/near_field_vehicle_demo.cpp's
// own GatherNearFieldCandidates() samples a REAL (if synthetic-grid)
// sr3airuntime::TrafficSegment road graph via ambient_population.h's
// GatherTrafficCandidates() - this project's playtest `city` loader has no
// real Traffic/Sidewalks `.czn_pc` content-kind reader wired in (the SAME
// honest gap this function's own doc comment already states), so there is
// no road graph to sample here either. CHOSEN stand-in instead: up to
// kNearFieldSpawnCandidateSlots (3) points scattered within an annulus
// [kNearFieldSpawnInnerM, kNearFieldSpawnOuterM) of the LIVE reference
// position, at an angle biased toward its current heading (+/-
// kNearFieldSpawnHeadingSpreadRad) so ambient traffic tends to actually
// appear ahead of a moving player rather than invisibly behind - purely
// cosmetic (the underlying despawn/cap/headroom algorithm this feeds is
// reused completely unmodified and does not care where candidates came
// from), not a traced fact.
constexpr double kNearFieldSpawnInnerM = 25.0;

constexpr double kNearFieldSpawnOuterM = 90.0;

constexpr double kNearFieldSpawnHeadingSpreadRad = 1.22173;  // ~70 degrees either side - CHOSEN, cosmetic only

} // namespace

namespace sr3viewer {

// Sec20.6.1(c) (freeroam_population.h): spawn points are where a road chord
// crosses the 130 m ring around the camera, kept only inside the speed-dependent
// moving cone. STAND-IN: no real road graph (needs the zone-data 13.2a graph), so
// each candidate is a SYNTHETIC radial chord whose crossing with the ring is found
// with ChordCircleCrossing. The 65 m inner-ring fallback (hidden-only, approximated
// by "not ahead of a moving camera") applies when no 130 m point survives.
//
// REAL GRAPH (feature/road-graph): when the playtest loaded the city road graph
// (g_playtestRoadGraph, spec-zone-data-format.md Sec13.2a) the candidates are real
// lane segments instead: GatherRoadVehicleSpawnPoints() (road_graph_placement.h) does
// the segment query, flag masks, capacity, chord/ring crossing, cone, closing test
// and 65 m hidden fallback. The synthetic chords below remain the fallback only when
// no road graph could be loaded (e.g. archives without sr3_city_0.vpp_pc).
const sr3zone::RoadGraph* g_playtestRoadGraph = nullptr;

std::vector<sr3airuntime::RoadSpawnPoint> g_lastRoadSpawnPoints;  // candidates of the most recent gather (lane lookup for the spawned car)

sr3airuntime::SegmentOccupancy g_roadOccupancy;  // live near-field vehicles per road segment (RG-4), rebuilt per pass

bool g_roadHighwayOnly = false;                    // Sec20.6.1(c) highway rule result for the current pass (RG-7)

// RG-3. CHOSEN: no streamed vehicle-slot set is modelled (the Sec20.6.1(e) streamed-set re-roll cadence is OPEN),
// so this stays empty and the mask falls back to 0x10400; filling it adds each group's Spline_Type bit.
std::vector<std::string> g_streamedVehicleSplineTypes;

float g_frustumViewProj[16] = {};                  // previous frame's row-vector view*projection (RG-5 hidden test)

bool g_haveFrustum = false;

size_t g_roadVehGathers = 0, g_roadVehCandidatesTotal = 0, g_roadVehLaneSpawns = 0;

size_t g_roadPedGathers = 0, g_roadPedEdgesTotal = 0;


std::vector<sr3airuntime::Vec3> GatherNearFieldSpawnCandidatesAroundPlayer(const sr3airuntime::Vec3& referencePos,
                                                                           const sr3airuntime::Vec3& cameraVelocity,
                                                                           sr3airuntime::DeterministicRng& rng) {
    std::vector<sr3airuntime::Vec3> out;
    const double speed = std::sqrt(cameraVelocity.x * cameraVelocity.x + cameraVelocity.z * cameraVelocity.z);
    const double minDot = sr3airuntime::MovingConeMinDot(speed);
    const bool moving = speed > sr3airuntime::kCameraMovingSpeed;
    const sr3airuntime::Vec3 vdir = moving ? sr3airuntime::Vec3{cameraVelocity.x / speed, 0.0, cameraVelocity.z / speed}
                                           : sr3airuntime::Vec3{0, 0, 0};
    if (g_playtestRoadGraph) {
        sr3airuntime::RoadVehicleSpawnQuery q;
        q.centre = referencePos;
        q.cameraVelocity = cameraVelocity;
        q.pickUniform = [&rng](size_t n) {
            const size_t i = static_cast<size_t>(sr3airuntime::RandomUnitFloat01(rng) * static_cast<double>(n));
            return n ? std::min(i, n - 1) : size_t{0};
        };
        // Sec20.6.1(c) "Segment full" (RG-4) and the bit-4 highway rule (RG-7): occupancy = live near-field
        // vehicles per segment, rebuilt by the caller each pass.
        q.occupancy = [](uint32_t s) { return g_roadOccupancy.Count(s); };
        q.highwayOnly = g_roadHighwayOnly;
        q.includeMask = sr3airuntime::BuildRoadSpawnIncludeMask(g_streamedVehicleSplineTypes);  // RG-3
        // Inner-ring "hidden" test (RG-5): the +-1.5 m box must be outside the camera frustum. Uses the previous
        // frame's view-projection (the camera is built later in the frame); the occlusion test (0x005dcad0) is not
        // modelled. Before the first frame: the old CHOSEN stand-in (visible = ahead of a moving camera).
        q.isVisible = [=](const sr3airuntime::Vec3& p) {
            if (g_haveFrustum) return sr3airuntime::BoxInFrustum(g_frustumViewProj, p, 1.5);
            if (!moving) return false;
            const double dx = p.x - referencePos.x, dz = p.z - referencePos.z;
            const double l = std::sqrt(dx * dx + dz * dz);
            return l > 1e-9 && (dx * vdir.x + dz * vdir.z) / l > 0.3;
        };
        g_lastRoadSpawnPoints = sr3airuntime::GatherRoadVehicleSpawnPoints(*g_playtestRoadGraph, q);
        ++g_roadVehGathers;
        g_roadVehCandidatesTotal += g_lastRoadSpawnPoints.size();
        for (const auto& rp : g_lastRoadSpawnPoints) out.push_back(rp.lanePos);  // placed on the lane polyline
        return out;
    }
    auto tryRing = [&](double radius, bool hiddenOnly) {
        for (int attempt = 0; attempt < 32 && out.size() < sr3airuntime::kNearFieldSpawnCandidateSlots; ++attempt) {
            const double ang = sr3airuntime::RandomUnitFloat01(rng) * 2.0 * 3.14159265358979323846;
            const sr3airuntime::Vec3 dir{std::cos(ang), 0.0, std::sin(ang)};
            const sr3airuntime::Vec3 a = referencePos + dir * (radius * 0.5);
            const sr3airuntime::Vec3 b = referencePos + dir * 200.0;
            sr3airuntime::Vec3 p;
            if (!sr3airuntime::ChordCircleCrossing(a, b, referencePos, radius, p)) continue;
            const double dot = dir.x * vdir.x + dir.z * vdir.z;
            if (moving && dot < minDot) continue;
            if (hiddenOnly && moving && dot > 0.3) continue;
            out.push_back(p);
        }
    };
    tryRing(130.0, false);
    if (out.empty()) tryRing(65.0, true);
    return out;
}

} // namespace sr3viewer

namespace {

// GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer (THIS TASK,
// near-field-peds-playtest, 2026-10-10) - the CHOSEN candidate-segment
// SOURCE runPlaytest() feeds SelectNearFieldPedSpawn() (via
// RunNearFieldPedSpawnPass's own candidateSegments parameter,
// near_field_ped_population.h), following
// GatherNearFieldSpawnCandidatesAroundPlayer() just above as its exact
// precedent. near_field_ped_population.h's own file header states this
// file does not own a real Sidewalks graph reader ("callers supply
// pre-resolved candidate geometry") - the SAME honest gap the vehicle
// function above already states for the Traffic graph, restated here for
// Sidewalks. CHOSEN stand-in: synthesize kNearFieldSpawnCandidateSlots (3,
// the SAME slot-count precedent reused, not a second literal) short
// SidewalkSegment records, each centered within a heading-biased annulus
// around the player, oriented along a second independent random angle (a
// segment, unlike a vehicle candidate point, needs a direction - purely
// cosmetic, consumed only by SelectNearFieldPedSpawn's own sideways-offset
// normal and PickWeightedIndex's own length-weighting, not a traced fact).
// UPDATED 2026-10-10 (ped-spawn-distance-spec-tuning task, spec-ai-runtime.md
// Sec20.1 re-read in full): the prior unit's own "DELIBERATELY NOT [25,90)"
// reasoning directly above was right about the ~90-unit floor being real and
// unconditional, but wrong about why candidates needed to sit at 95m+ in
// EVERY direction. Sec20.1's own text on the creation routine (0x0090b0a0)
// lists three SEPARATE reject conditions, not a single distance band:
// "rejects within ~90 units of the camera (no pop-in next to the player) and
// beyond a second distance threshold; rejects if visible to the current (or
// splitscreen) camera" - the ~90-unit floor applies regardless of
// visibility (so peds genuinely cannot spawn closer than that, on- or
// off-screen), but the VISIBILITY test is a SEPARATE gate on top of it: nothing
// in the spec says a candidate beyond 90 units must also be ahead of the
// player. The prior unit's own heading-biased cone (kNearFieldSpawnHeadingSpreadRad,
// above) always placed every candidate roughly in front of the camera, which
// is exactly the retail-inaccurate case this task was asked to fix (peds
// popping in directly in view). Candidates are now scattered across a FULL
// circle (no heading bias) so a visibility gate actually has something to
// differentiate - near_field_ped_population.h's own ApproxVisibleToCamera
// (a CHOSEN, honestly-approximate horizontal-FOV cone test, not a recovered
// engine frustum) is wired at RunNearFieldPedSpawnPass's own call site below,
// so a candidate behind/beside the player can now be accepted much closer to
// the real ~90-unit floor instead of needing the prior unit's own 95m+ CHOSEN
// margin in every direction, while one ahead of the player in view is
// rejected the same way the real engine's own check would reject it.
constexpr double kNearFieldPedSpawnInnerM = 10.0;   // spec 20.6.2(c): accept band 10-90 m (was 90.5 under the superseded 90 m near reject)

constexpr double kNearFieldPedSpawnOuterM = 70.0;   // spec 20.6.2(c): candidate edges within 70 m of the camera (was 160). Synthetic stand-in edges; real edges need the 13.2a sidewalk graph.

constexpr double kNearFieldPedSpawnSegmentHalfLengthM = 6.0;  // CHOSEN, cosmetic only - a short sidewalk stand-in segment, not a real graph edge length

} // namespace

namespace sr3viewer {

std::vector<sr3airuntime::SidewalkSegment> GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(
    const sr3airuntime::Vec3& referencePos, double /*referenceHeadingRad*/, sr3airuntime::DeterministicRng& rng) {
    // REAL GRAPH (feature/road-graph): real sidewalk edges within 70 m (<= 100), spec-ai-runtime.md
    // Sec20.6.2(c). Rebuilt on every ped tick (200-300 ms) rather than the engine's 500 ms / 30 m
    // cache rule - a perf detail only; membership is identical or fresher.
    if (g_playtestRoadGraph) {
        auto edges = sr3airuntime::GatherRoadPedCandidateEdges(*g_playtestRoadGraph, referencePos,
                                                               kNearFieldPedSpawnOuterM, 100);
        ++g_roadPedGathers;
        g_roadPedEdgesTotal += edges.size();
        return edges;
    }
    std::vector<sr3airuntime::SidewalkSegment> out;
    for (size_t i = 0; i < sr3airuntime::kNearFieldSpawnCandidateSlots; ++i) {
        // Full circle, NOT heading-biased (see this function's own 2026-10-10
        // doc-comment update above) - the visibility gate at the call site is
        // what now decides whether an in-view candidate is actually accepted,
        // not the generation-side angle.
        const double angle = sr3airuntime::RandomUnitFloat01(rng) * 2.0 * 3.14159265358979323846;
        const double radius =
            kNearFieldPedSpawnInnerM +
            sr3airuntime::RandomUnitFloat01(rng) * (kNearFieldPedSpawnOuterM - kNearFieldPedSpawnInnerM);
        sr3airuntime::Vec3 center = referencePos;
        center.x += radius * std::cos(angle);
        center.z += radius * std::sin(angle);
        // Second independent random angle for the segment's own direction
        // (CHOSEN, cosmetic - no relation to the player's heading; a real
        // sidewalk segment's own direction is a graph-topology fact this
        // file does not have access to).
        const double segAngle = sr3airuntime::RandomUnitFloat01(rng) * 2.0 * 3.14159265358979323846;
        sr3airuntime::SidewalkSegment seg;
        seg.id = static_cast<uint64_t>(i) + 1;
        seg.start = center;
        seg.start.x -= kNearFieldPedSpawnSegmentHalfLengthM * std::cos(segAngle);
        seg.start.z -= kNearFieldPedSpawnSegmentHalfLengthM * std::sin(segAngle);
        seg.end = center;
        seg.end.x += kNearFieldPedSpawnSegmentHalfLengthM * std::cos(segAngle);
        seg.end.z += kNearFieldPedSpawnSegmentHalfLengthM * std::sin(segAngle);
        out.push_back(seg);
    }
    return out;
}

} // namespace sr3viewer

namespace {

// --mission=<stem> (Phase 1, this task): the stem-resolution/mission-start/
// watchdog helpers runPlaytest() below uses. See that function's own doc
// comment (right below this namespace) for the full rationale/citation -
// not repeated here. Anonymous namespace, same convention every other
// small helper block in this file uses (e.g. readLuaGlobalNumber/
// drawHudLabel above) - internal linkage, usable by any later function in
// this one translation unit.

// One real row of tools/mission_package_per_container.tsv this pass needs -
// a trimmed copy of tools/lua_host_run.cpp's own MissionPackageRow (that
// file's columns 4/6/7/8; container_name is kept too, for a readable error
// message when a stem resolves to an entry name this command's own given
// archives don't contain).
struct MissionPackageRow {
    std::string containerName;
    std::string missionLuaEntryName;
    std::string missionLuaStem;
    bool coversStartScript = false;
};


// Manual tab-split - the SAME tolerant-of-a-missing-trailing-empty-field
// convention tools/lua_host_run.cpp's own loadMissionPackageRows() uses
// (std::getline does not emit a trailing empty token the way some TSV
// readers do).
std::vector<MissionPackageRow> loadMissionPackageRows(const std::string& path) {
    std::vector<MissionPackageRow> out;
    std::ifstream f(path);
    if (!f) return out; // real, checked negative if missing - reported by the caller
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; } // header row
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cols;
        std::istringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) cols.push_back(cell);
        if (cols.size() < 8) continue; // malformed-row skip, not expected against this file's own real output
        MissionPackageRow r;
        r.containerName = cols[3];
        r.missionLuaEntryName = cols[5];
        r.missionLuaStem = cols[6];
        r.coversStartScript = (cols[7] == "1");
        out.push_back(std::move(r));
    }
    return out;
}

struct MissionWalkState {
    std::deque<std::vector<uint8_t>> bufferPool; // keeps decompressed nested-container bytes alive (same reason
                                                  // tools/lua_host_run.cpp's own WalkState::bufferPool does - a
                                                  // vpp::Container built over one of these views it)
    std::vector<MissionScriptInstance> found;
};

void missionWalkInspectLeaf(MissionWalkState& st, const std::string& name, const uint8_t* data, size_t len) {
    if (endsWithNoCase(name, ".lua")) {
        MissionScriptInstance si;
        si.entryName = name;
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

// Copied and adapted, deliberately, from tools/lua_host_run.cpp's own
// WalkState/walk() (same recursion into nested `.str2_pc` containers via
// vpp::Container::openNested()/decompressEntry(), same ".lua name suffix" +
// "Lua bytecode magic" leaf test) - this project's own established
// convention (that file's own top comment: "duplicated with attribution
// instead, matching how vpp_dump.cpp/vpp_extract.cpp already each carry
// their own walk rather than sharing one"), not refactored into a shared
// header.
void missionWalk(MissionWalkState& st, const vpp::Container& c) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (e.name.empty()) continue; // unnamed entries (hash-only) are never a ".lua" leaf match
        const std::string& name = e.name;
        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                missionWalk(st, child);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                missionWalkInspectLeaf(st, name, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        st.bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(st.bufferPool.back().data(), st.bufferPool.back().size()));
                            nested = true;
                            missionWalk(st, child);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = st.bufferPool.back();
                            missionWalkInspectLeaf(st, name, buf.data(), buf.size());
                        }
                        continue;
                    }
                }
                missionWalkInspectLeaf(st, name, result.data.data(), result.data.size());
            }
            // decompress failure: silently skipped, same as lua_host_run.cpp's own walk()
            // (that file's own st.stats.decompressFailed counter has no equivalent need here -
            // this is a single-stem lookup, not a population census).
        }
    }
}

} // namespace

namespace sr3viewer {

// Walks `archivePaths` ONCE, collecting the first-found-by-entry-name real
// `.lua` content for every name in `wantedLower` (case-insensitive exact
// match, first archive in the given order wins, same convention tools/
// lua_host_run.cpp's own st.found/first-found-instance game_lib.lua block
// uses) - a single combined pass rather than one walk per name, since this
// task's own mission bind needs several names at once (see
// missionPreloadNamed() below).
std::map<std::string, MissionScriptInstance> findScriptsByName(const std::vector<std::string>& archivePaths,
                                                                 const std::set<std::string>& wantedLower) {
    std::map<std::string, MissionScriptInstance> out;
    for (const auto& archivePath : archivePaths) {
        if (out.size() >= wantedLower.size()) break; // every wanted name already found
        std::vector<uint8_t> bytes = readFileLargeC(archivePath);
        if (bytes.empty()) continue; // unreadable archive - try the next one, not fatal to the search itself
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            MissionWalkState st;
            missionWalk(st, root);
            for (auto& f : st.found) {
                const std::string lower = toLowerLocal(f.entryName);
                if (wantedLower.count(lower) && !out.count(lower)) out[lower] = std::move(f);
            }
        } catch (const std::exception&) {
            // unreadable/unparseable archive - try the next one, same best-effort convention
        }
    }
    return out;
}

} // namespace sr3viewer

namespace sr3viewer {
namespace missionzoneobj {

// --- Real zone-object (trigger position) load for --mission (2026-10-10,
// in-world-trigger task) -----------------------------------------------
//
// Copied and adapted, deliberately, from tools/lua_host_run.cpp's own
// zoneobj::walkCzn()/installZoneObjectsFromCache() (same recursion shape as
// missionWalk() just above - this project's own established "duplicated
// with attribution, not factored into a shared header" convention, per
// that file's own top comment). Walks the SAME `cityArchivePaths` this
// command already opens for mission-script resolution (findScriptsByName
// above), looking for `.czn_pc` leaves, and ingests every complete Objects
// (0x2234) section found via sr3zone::ZoneObjects::locate() + hudEs->
// loadZoneObjectsSection() - the exact same real reader/ingest pair
// tools/zone_object_resolver_probe.cpp already proved against real data
// (the `Killbane` boss-character example). This gives runPlaytest() a real
// name -> sr3zone::ObjectRecord resolver it can read a real trigger's
// position from via sr3zone::ObjectPosition() - see this function's own
// caller for the honest shape/extent gap this does NOT close (Sec1.14's
// own "on-disk property-id encoding of shape/extents/radius is unknown"
// finding).

bool endsWithCznExtLocal(const std::string& lowerName) {
    static const std::string ext = ".czn_pc";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}


struct Counters {
    uint64_t cznFilesFound = 0;
    uint64_t cznWithSection = 0;
};


void ingestCznBuffer(sr3luahost::EngineState& es, Counters& ctr, const uint8_t* data, size_t len) {
    ++ctr.cznFilesFound;
    std::vector<sr3zone::ObjectsSection> sections;
    try {
        sections = sr3zone::ZoneObjects::locate(vpp::ByteView(data, len));
    } catch (const std::exception&) {
        return;
    }
    for (const sr3zone::ObjectsSection& section : sections) {
        if (!section.complete) continue;
        ++ctr.cznWithSection;
        es.loadZoneObjectsSection(section);
    }
}


void walkCzn(sr3luahost::EngineState& es, Counters& ctr, const vpp::Container& c) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? std::string() : e.name;
        bool isCzn = endsWithCznExtLocal(toLowerLocal(name));

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walkCzn(es, ctr, child);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested && isCzn) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                ingestCznBuffer(es, ctr, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            bool ok = result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch ||
                      result.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                      result.status == vpp::DecodeStatus::ContentValidated ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStream ||
                      result.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
            if (!ok) continue;
            bool nested = false;
            if (result.data.size() >= 4) {
                uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                  ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                if (magic == 0x51890ACEu) {
                    try {
                        vpp::Container child(vpp::ByteView(result.data.data(), result.data.size()));
                        nested = true;
                        walkCzn(es, ctr, child);
                    } catch (const std::exception&) {
                        nested = false;
                    }
                }
            }
            if (!nested && isCzn) ingestCznBuffer(es, ctr, result.data.data(), result.data.size());
        }
    }
}


// ce4556f Sec28.2 (m09 Girls_heli): copied from tools/lua_host_run.cpp's
// installVehicleTypeClassTableFromCache. Builds the real `<name>_veh.xtbl`
// type-name -> Vehicle_Type class table from whichever of misc_tables/dlc1-3
// are in `archivePaths`, and installs it BEFORE the zone ingest so a placed
// script_vehicle's class (3 Helicopter / 4 Vtol -> heli gate 0x00ad31a0) is
// resolved from data. Never fatal.
std::string installVehicleTypeClassTableFromArchives(const std::vector<std::string>& archivePaths,
                                                      sr3luahost::EngineState& es) {
    std::unordered_map<std::string, int32_t> classByLowerName;
    std::unordered_map<std::string, int32_t> specialByLowerName;
    std::unordered_set<std::string> effectNamesLower;
    size_t filesParsed = 0;
    std::function<void(vpp::Container&)> walk = [&](vpp::Container& c) {
        const auto& entries = c.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& name = entries[i].name;
            std::string lname = toLowerLocal(name);
            if (lname.size() > 9 && lname.compare(lname.size() - 9, 9, "_veh.xtbl") == 0) {
                try {
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (!(r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::SizeMismatch ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::ContentValidated ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                              r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain))
                            continue;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                    for (const auto& row : sr3vehicleinfo::ParseAllVehicles(doc)) {
                        if (!row.name.has_value() || row.name->empty()) continue;
                        // Special_Vehicle_Type (+0x4C4; a missing tag is 0 - spec Sec33.1 blocker m10).
                        specialByLowerName[sr3luahost::AsciiLowerFold(*row.name)] = row.specialVehicleType < 0 ? 0 : row.specialVehicleType;
                        if (row.vehicleType.vehicleClass == sr3vehicleinfo::VehicleClass::Unknown) continue;
                        classByLowerName[sr3luahost::AsciiLowerFold(*row.name)] =
                            static_cast<int32_t>(row.vehicleType.vehicleClass);
                    }
                    ++filesParsed;
                } catch (const std::exception&) {
                }
                continue;
            }
            // Effects tables (effects.xtbl + <dlc>_effects.xtbl): collect every Effect Name so the effect-index
            // resolver 0x005c50b0 (spec-tables-environment.md 10.1) can answer "index != -1" from real data.
            if (lname == "effects.xtbl" || (lname.size() > 13 && lname.compare(lname.size() - 13, 13, "_effects.xtbl") == 0 &&
                                            lname != "skybox_effects.xtbl" && lname != "interface_effects.xtbl")) {
                try {
                    std::string fileBytes;
                    if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                        vpp::ByteView raw = c.rawEntryBytes(i);
                        fileBytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
                    } else {
                        vpp::DecompressResult r = c.decompressEntry(i);
                        if (!(r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::SizeMismatch ||
                              r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                              r.status == vpp::DecodeStatus::ContentValidated))
                            continue;
                        fileBytes.assign(reinterpret_cast<const char*>(r.data.data()), r.data.size());
                    }
                    sr3xtbl::Document doc = sr3xtbl::ParseDocument(fileBytes);
                    for (const auto& eff : sr3tables_environment::ParseEffectsTable(doc))
                        if (eff.name.has_value() && !eff.name->empty())
                            effectNamesLower.insert(sr3luahost::AsciiLowerFold(*eff.name));
                } catch (const std::exception&) {
                }
                continue;
            }
            if (lname.size() >= 4 && lname.compare(lname.size() - 4, 4, "xtbl") == 0) continue;
            if (entries[i].payload.kind != vpp::PayloadKind::Raw) {
                try {
                    vpp::DecompressResult r = c.decompressEntry(i);
                    bool good = r.status == vpp::DecodeStatus::Ok || r.status == vpp::DecodeStatus::SizeMismatch ||
                                r.status == vpp::DecodeStatus::OkUnconfirmedContent ||
                                r.status == vpp::DecodeStatus::ContentValidated ||
                                r.status == vpp::DecodeStatus::RecoveredSharedStream ||
                                r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain;
                    if (!good || r.data.size() < 4) continue;
                    uint32_t magic = (uint32_t)r.data[0] | ((uint32_t)r.data[1] << 8) | ((uint32_t)r.data[2] << 16) |
                                     ((uint32_t)r.data[3] << 24);
                    if (magic != 0x51890ACEu) continue;
                    vpp::Container nested(vpp::ByteView(r.data.data(), r.data.size()));
                    walk(nested);
                } catch (const std::exception&) {
                }
                continue;
            }
            try {
                vpp::Container nested(c.rawEntryBytes(i));
                walk(nested);
            } catch (const std::exception&) {
            }
        }
    };
    // The vehicle tables live in the cache dir next to the mission archives
    // (misc_tables.vpp_pc is not itself in `archivePaths`), so resolve the
    // four archives from the first archive's own directory.
    std::vector<std::string> wanted;
    if (!archivePaths.empty()) {
        std::filesystem::path dir = std::filesystem::path(archivePaths.front()).parent_path();
        for (const char* n : {"misc_tables.vpp_pc", "dlc1.vpp_pc", "dlc2.vpp_pc", "dlc3.vpp_pc"}) {
            std::error_code ec;
            std::filesystem::path p = dir / n;
            if (std::filesystem::is_regular_file(p, ec)) wanted.push_back(p.string());
        }
    }
    for (const auto& archivePath : wanted) {
        std::vector<uint8_t> bytes = readFileLargeC(archivePath);
        if (bytes.empty()) continue;
        try {
            vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
            walk(c);
        } catch (const std::exception&) {
        }
    }
    es.installVehicleTypeClassTable(classByLowerName);
    es.installVehicleSpecialTypeTable(specialByLowerName);
    es.installEffectNameTable(std::move(effectNamesLower));
    return "vehicle class table: files=" + std::to_string(filesParsed) +
           " types_resolved=" + std::to_string(es.vehicleTypeClassTableSize());
}


// Walks every archive in `archivePaths` (the SAME list --mission already
// resolves scripts against) and ingests every real `.czn_pc` Objects
// section found into `es`. Returns a short human-readable summary, same
// "never fatal, always report what happened" convention as every other
// --mission bind-time diagnostic line in this function.
std::string installZoneObjectsFromArchives(const std::vector<std::string>& archivePaths, sr3luahost::EngineState& es) {
    Counters ctr;
    size_t archivesRead = 0;
    for (const auto& archivePath : archivePaths) {
        std::vector<uint8_t> bytes = readFileLargeC(archivePath);
        if (bytes.empty()) continue;
        ++archivesRead;
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walkCzn(es, ctr, root);
        } catch (const std::exception&) {
            // unreadable/unparseable archive - try the next one, same best-effort convention
        }
    }
    return "archives_read=" + std::to_string(archivesRead) + "/" + std::to_string(archivePaths.size()) +
           " czn_files_found=" + std::to_string(ctr.cznFilesFound) +
           " objects_sections=" + std::to_string(ctr.cznWithSection) +
           " named_objects_resolved=" + std::to_string(es.zoneObjectsLoadedCount());
}


// --- Real zscene/cutscene scene table for --mission (2026-10-10, coordinator-
// routed fix): `tools/lua_host_run.cpp`'s own installSceneTableFromCache()
// (its top comment, "The zscene / cutscene scene table") already builds this
// from REAL `cutscene.xtbl` + per-scene `<name>.cte_xtbl` data
// (sr3tables_cutscene::BuildMainSceneTable()) and installs it via
// EngineState::installZsceneTable() - but that wiring only runs in that
// OTHER tool's own mission-drive loop, never in this command's own
// runPlaytest() --mission=<stem> live path. Without it, zsceneResolve()
// (src/lua_cutscene.cpp) falls back to the OPEN per-name map
// (zsceneLoadable_), which has never been told anything about "09_in"/
// "05_in" - so EngineState::zsceneIsLoaded() throws OpenStateError on its
// very first call instead of ever reaching Sec14.23's own kind-gate (a name
// not resolving to a kind-1 entry returns `true` AT ONCE, Sec26.25 Scene
// entry: `CutsceneType` "Story" -> kind 2, not 1 - m09/m05's own `_in`/`_out`
// scenes are story cutscenes, not Zscenes, so the real engine never actually
// preps them as Zscenes and the check passes immediately). This is the SAME
// kind-gate code path lua_host_run.cpp's own mission-drive runs already
// exercise correctly - the gap was this command never installing the real
// table, not a missing kind check in EngineState::zsceneIsLoaded() itself
// (re-read directly, src/lua_cutscene.cpp: the `if (!r.loadable) return
// true;` line already implements Sec14.23's fast-path exactly).
//
// Copied and adapted from lua_host_run.cpp's own installSceneTableFromCache
// (same duplicated-with-attribution convention as walkCzn()/
// installZoneObjectsFromArchives() just above) - the one real difference is
// that this command has no cache DIRECTORY to iterate, only the specific
// `cityArchivePaths` its own argv already named (same adaptation
// installZoneObjectsFromArchives() already makes for zone data): this scans
// those paths for one whose filename matches "cutscene_tables.vpp_pc"
// (case-insensitive), same as lua_host_run's own directory_iterator search.
// A mission whose scene table lives in a DLC-only container needs that
// DLC's own cutscene_tables archive passed the same way a DLC mission's own
// city archive is (this command's own "[<more city archives>...]" usage
// text) - not attempted here beyond the "main" container, matching
// lua_host_run's own "only the 'main' call runs" scope limit.
std::string installZsceneTableFromArchives(const std::vector<std::string>& archivePaths, sr3luahost::EngineState& es) {
    std::string archivePath;
    for (const auto& p : archivePaths) {
        const std::string lower = toLowerLocal(p);
        static const std::string want = "cutscene_tables.vpp_pc";
        if (lower.size() >= want.size() &&
            lower.compare(lower.size() - want.size(), want.size(), want) == 0) {
            archivePath = p;
            break;
        }
    }
    if (archivePath.empty()) {
        return "not installed (no cutscene_tables.vpp_pc among the given archives; the table stays OPEN - "
               "pass it as one of this command's own [<more city archives>...] tokens)";
    }
    std::vector<uint8_t> bytes = readFileLargeC(archivePath);
    if (bytes.empty()) return "not installed (could not read " + archivePath + ")";
    try {
        vpp::Container c(vpp::ByteView(bytes.data(), bytes.size()));
        auto entryBytes = [&c](const std::string& wanted, std::string* foundName) -> std::optional<std::string> {
            const std::string w = toLowerLocal(wanted);
            const auto& entries = c.entries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (toLowerLocal(entries[i].name) != w) continue;
                if (foundName) *foundName = entries[i].name;
                if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
                }
                vpp::DecompressResult r = c.decompressEntry(i);
                if (r.status != vpp::DecodeStatus::Ok) return std::nullopt;
                return std::string(reinterpret_cast<const char*>(r.data.data()), r.data.size());
            }
            return std::nullopt;
        };
        std::optional<std::string> names = entryBytes("cutscene.xtbl", nullptr);
        if (!names) {
            return "not installed (cutscene.xtbl not readable in " + archivePath + ")";
        }
        sr3tables_cutscene::SceneTable table = sr3tables_cutscene::BuildMainSceneTable(
            *names, [&entryBytes](const std::string& fileName) -> std::optional<sr3tables_cutscene::OpenedFile> {
                sr3tables_cutscene::OpenedFile f;
                std::optional<std::string> b = entryBytes(fileName, &f.fileName);
                if (!b) return std::nullopt;
                f.bytes = std::move(*b);
                return f;
            });
        std::vector<sr3luahost::EngineState::ZsceneTableRow> rows;
        size_t kind0 = 0, kind1 = 0, kind2 = 0, kindOpen = 0;
        for (const auto& e : table.entries) {
            sr3luahost::EngineState::ZsceneTableRow row;
            row.name = e.name;
            row.crc = e.crc;
            row.kind = e.fields.kind;
            row.cameraScriptName = e.fields.cameraScriptText;
            if (e.fields.resources) {
                std::vector<std::string> res;
                for (const auto& r : *e.fields.resources) {
                    res.push_back(r.source == sr3tables_cutscene::SceneResource::Source::AngelFixedNames
                                      ? std::string("(IsAngel: three fixed names, OPEN)")
                                      : (r.textPresent ? r.text : std::string("(Variant absent, OPEN)")));
                }
                row.resources = std::move(res);
            }
            rows.push_back(std::move(row));
            if (!e.fields.kind) ++kindOpen;
            else if (*e.fields.kind == 0) ++kind0;
            else if (*e.fields.kind == 1) ++kind1;
            else ++kind2;
        }
        const size_t installed = rows.size();
        es.installZsceneTable(std::move(rows));
        return "installed from " + archivePath + " entries=" + std::to_string(installed) +
               " kind0(Designer)=" + std::to_string(kind0) + " kind1(Zscene)=" + std::to_string(kind1) +
               " kind2(Story)=" + std::to_string(kind2) + " kindOpen=" + std::to_string(kindOpen);
    } catch (const std::exception& ex) {
        return std::string("not installed (exception: ") + ex.what() + ")";
    }
}

} // namespace missionzoneobj
} // namespace sr3viewer

namespace sr3viewer {


// --- Cache-directory archive-scan fallback (this task, fix/m05-zscene-
// m13-systemlib, 2026-10-12): `tools/lua_host_run.cpp`'s own mission-drive
// loop resolves archives it needs (e.g. continuous_explosions.xtbl's
// installContinuousExplosionTableFromCache() above, same file) by scanning
// EVERY file in the cache DIRECTORY, so it always finds `system_lib.lua`
// (in `patch_compressed.vpp_pc`) and the real zscene/cutscene scene table
// (in `cutscene_tables.vpp_pc`) regardless of what the caller happened to
// name on the command line. `sr3_viewer.cpp`'s own `playtest` command instead
// only ever looks at the archives the CALLER explicitly listed
// (`cityArchivePaths`, parsed directly from argv) - so a user who does not
// already know (or forgets to pass) `cutscene_tables.vpp_pc`/
// `patch_compressed.vpp_pc` hits `zscene_is_loaded`'s OPEN per-name-map
// fallback (missionzoneobj::installZsceneTableFromArchives's own doc
// comment above) or `_GetAnyGlobalSilent is not a function` (system_lib.lua
// never preloaded) even though the real data is sitting right next to the
// archives they DID pass. Root cause for BOTH Task A and Task B blockers
// is this one gap - this command's own caller-supplied-list requirement -
// not a missing kind-gate (zsceneIsLoaded's fast path is already correct,
// confirmed directly above) and not a missing preload call (preloadNamed's
// own two-name block below already runs, it just never finds the archive).
//
// Fix: after parsing the caller's own `cityArchivePaths`, scan the
// DIRECTORY that holds the first one (every real cache archive lives
// together in one flat directory - same assumption lua_host_run.cpp's own
// `fs::directory_iterator(cacheDir)` calls already make) and append any
// sibling `.vpp_pc` file whose lower-cased name matches one this command's
// own mission/Lua-preload path is known to need, skipping any name already
// present (case-insensitive) in the caller's own list so an explicit
// `--` archive is never shadowed or duplicated. Best-effort only: a
// filesystem error (missing directory, permission) leaves the original
// list untouched, same fallback convention installZsceneTableFromArchives's
// own try/catch already uses.
std::vector<std::string> expandArchivePathsWithCacheDirScan(std::vector<std::string> archivePaths) {
    if (archivePaths.empty()) return archivePaths;
    namespace fs = std::filesystem;
    static const char* const kWantedNames[] = {
        "cutscene_tables.vpp_pc",  // Task A: real zscene/cutscene scene table (Sec26.25)
        "patch_compressed.vpp_pc", // lua_host_run.cpp's own kPreferredArchives order (first choice,
                                   // kept as a fallback candidate even though system_lib.lua's real
                                   // home turned out to be interface.vpp_pc - see below)
        "misc_tables.vpp_pc",      // lua_host_run.cpp's own fallback for the same lookups
        "interface.vpp_pc",        // CORRECTED (2026-10-12, 49-mission-sweep task): Task B's own
                                   // premise ("system_lib.lua lives in patch_compressed.vpp_pc") was
                                   // wrong - every real per-script census this project has produced
                                   // (verdict_per_script.tsv, many results/ runs, e.g.
                                   // results/verify_actionplay_mission_drive/verdict_per_script.tsv)
                                   // shows system_lib.lua's real container is interface.vpp_pc, not
                                   // patch_compressed.vpp_pc. Without this name, findScriptsByName()
                                   // below never finds it via this command's own curated cache-dir
                                   // scan (confirmed directly: 49-mission sweep's own m01 run hit
                                   // "BLOCKING FINDING - no entry named system_lib.lua found" despite
                                   // the scan already running, then game_lib.lua's own first helper
                                   // call failed with "attempt to call a nil value" on
                                   // _PrepareForDynamicGlobals/_DynamicGlobalsLoadComplete, both
                                   // defined inside system_lib.lua itself per spec-lua-bindings.md
                                   // Sec16.2's own load-order requirement).
    };
    std::error_code ec;
    fs::path first(archivePaths.front());
    fs::path cacheDir = first.parent_path();
    if (cacheDir.empty() || !fs::is_directory(cacheDir, ec) || ec) return archivePaths;

    auto toLowerStr = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::vector<std::string> haveLower;
    haveLower.reserve(archivePaths.size());
    for (const auto& p : archivePaths) haveLower.push_back(toLowerStr(fs::path(p).filename().string()));

    for (const char* wanted : kWantedNames) {
        const std::string wantedLower = toLowerStr(wanted);
        if (std::find(haveLower.begin(), haveLower.end(), wantedLower) != haveLower.end()) continue; // already named
        fs::directory_iterator it(cacheDir, ec);
        if (ec) break;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || ec) continue;
            if (toLowerStr(it->path().filename().string()) != wantedLower) continue;
            archivePaths.push_back(it->path().string());
            haveLower.push_back(wantedLower);
            break;
        }
    }
    return archivePaths;
}

MissionResolution resolveMissionStemScript(const std::vector<std::string>& archivePaths,
                                            const std::string& missionPackageTsvPath, const std::string& stem) {
    MissionResolution out;
    std::vector<MissionPackageRow> rows = loadMissionPackageRows(missionPackageTsvPath);
    if (rows.empty()) {
        out.error = "could not read/parse '" + missionPackageTsvPath + "' (0 rows) - pass its real path via "
                    "--mission-package-tsv if it is not next to the registration list";
        return out;
    }
    const MissionPackageRow* match = nullptr;
    for (auto& r : rows) {
        // SAME filter tools/lua_host_run.cpp's own mission-drive loop dedup applies
        // (coversStartScript==1, first match in file order - this project's own
        // 49-distinct-stem population is already deduped this way in that file).
        if (r.coversStartScript && r.missionLuaStem == stem) { match = &r; break; }
    }
    if (!match) {
        out.error = "stem '" + stem + "' not found in '" + missionPackageTsvPath + "' (no row with "
                    "mission_lua_stem==this AND stem_covers_a_start_script==1)";
        return out;
    }
    out.containerName = match->containerName;
    const std::string wantLower = toLowerLocal(match->missionLuaEntryName);
    auto found = findScriptsByName(archivePaths, {wantLower});
    auto it = found.find(wantLower);
    if (it == found.end()) {
        out.error = "stem '" + stem + "' resolves to entry '" + match->missionLuaEntryName + "' (container '" +
                    match->containerName + "') but that entry was not found in any of the " +
                    std::to_string(archivePaths.size()) + " given archive(s) - pass its containing .vpp_pc as one "
                    "of this command's own city-archive arguments";
        return out;
    }
    out.ok = true;
    out.entryName = it->second.entryName;
    out.content = std::move(it->second.content);
    return out;
}

void missionWatchdogHook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L,
               "sr3_viewer --mission: call exceeded the %d-instruction budget (likely an infinite loop in "
               "never-before-executed mission code) - aborted by this tool's own safety hook, not hung",
               kMissionWatchdogInstructionBudget);
}

// Watchdog-wrapped host.runChunk() - same convention as tools/lua_host_run.cpp's
// own runChunkWithWatchdog().
sr3luahost::Host::RunResult missionRunChunkWithWatchdog(sr3luahost::Host& host, lua_State* L,
                                                         const std::string& source, const std::string& chunkName) {
    lua_sethook(L, missionWatchdogHook, LUA_MASKCOUNT, kMissionWatchdogInstructionBudget);
    sr3luahost::Host::RunResult r = host.runChunk(L, source, chunkName);
    lua_sethook(L, nullptr, 0, 0); // always clear - never leaves the watchdog armed for a later, unrelated call
    return r;
}


FootstepCadenceSfxClip findFirstPlayableSfxClip(const std::string& archivePath, std::string& diagnosticLog) {
    FootstepCadenceSfxClip result;
    std::vector<uint8_t> archiveBytes = readFileLargeC(archivePath);
    if (archiveBytes.empty()) {
        diagnosticLog += "sfx-lookup: could not read archive " + archivePath + "\n";
        return result;
    }
    vpp::Container container(vpp::ByteView(archiveBytes.data(), archiveBytes.size()));

    auto endsWithLocal = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    for (size_t i = 0; i < container.entries().size(); ++i) {
        const std::string& plainName = container.entries()[i].name;
        if (!endsWithLocal(plainName, ".bnk_pc") || endsWithLocal(plainName, "_media.bnk_pc")) continue;
        const std::string stem = plainName.substr(0, plainName.size() - std::string(".bnk_pc").size());
        const std::string mediaName = stem + "_media.bnk_pc";
        size_t mediaIndex = static_cast<size_t>(-1);
        for (size_t j = 0; j < container.entries().size(); ++j) {
            if (container.entries()[j].name == mediaName) { mediaIndex = j; break; }
        }
        if (mediaIndex == static_cast<size_t>(-1)) continue;

        auto readEntry = [&](size_t idx, std::vector<uint8_t>& storage) -> vpp::ByteView {
            if (container.entries()[idx].payload.kind == vpp::PayloadKind::Raw) return container.rawEntryBytes(idx);
            auto r = container.decompressEntry(idx);
            storage = std::move(r.data);
            return vpp::ByteView(storage.data(), storage.size());
        };

        std::vector<uint8_t> plainStorage, mediaStorage;
        vpp::ByteView plainBytes, mediaBytes;
        try {
            plainBytes = readEntry(i, plainStorage);
            mediaBytes = readEntry(mediaIndex, mediaStorage);
        } catch (const std::exception& ex) {
            diagnosticLog += "sfx-lookup: " + stem + " read failed: " + ex.what() + "\n";
            continue;
        }
        if (plainBytes.size() == 0 || mediaBytes.size() == 0) continue;

        std::vector<sr3audio::HircItem> hircItems;
        try {
            auto hircChunk = sr3audio::findHircChunk(plainBytes);
            if (!hircChunk) continue;
            hircItems = sr3audio::walkHircItems(*hircChunk);
        } catch (const std::exception& ex) {
            diagnosticLog += "sfx-lookup: " + stem + " HIRC walk failed: " + ex.what() + "\n";
            continue;
        }

        sr3audio::MediaBank mb;
        std::vector<sr3audio::MediaBankRecord> mediaRecords;
        try {
            mb = sr3audio::MediaBank::parse(mediaBytes);
            mediaRecords = mb.walk(container.entries()[mediaIndex].payload.decompressedLength);
        } catch (const std::exception& ex) {
            diagnosticLog += "sfx-lookup: " + stem + " media-bank parse failed: " + ex.what() + "\n";
            continue;
        }

        size_t kind2Count = 0, resolvedCount = 0, vorbCount = 0, decodeOkCount = 0;
        for (const auto& item : hircItems) {
            if (item.kind != sr3audio::kHircKindSound) continue;
            ++kind2Count;
            sr3audio::ResolvedMedia resolved;
            try {
                resolved = sr3audio::resolveSoundIdToMedia(hircItems, item.id, mediaRecords);
            } catch (const std::exception&) {
                continue;  // not every Sound item is the plain kPlainCodec shape this resolves - expected, not an error
            }
            ++resolvedCount;
            if (resolved.size < 128) continue;
            const uint8_t* recBytes = mediaBytes.data() + resolved.offset;
            auto vorbLoc = sr3audioruntime::findRiffSubChunk(recBytes, resolved.size, sr3audioruntime::kFourCcVorb);
            if (!vorbLoc) continue;  // not every record is Vorbis-wrapped the way this pipeline targets
            ++vorbCount;
            sr3audioruntime::DecodeAttemptResult decoded =
                sr3audioruntime::attemptWwiseVorbisDecode(recBytes, resolved.size);
            if (!decoded.ok) continue;
            ++decodeOkCount;

            result.valid = true;
            result.soundObjectId = item.id;
            result.bankStem = stem;
            result.decoded = std::move(decoded);
            char idHex[16];
            std::snprintf(idHex, sizeof(idHex), "%08x", item.id);
            diagnosticLog += "sfx-lookup: resolved+decoded real SFX media, bank='" + stem +
                              "' soundObjectId=0x" + std::string(idHex) +
                              " mediaRecordIndex=" + std::to_string(resolved.mediaRecordIndex) +
                              " channels=" + std::to_string(result.decoded.channels) +
                              " sampleRate=" + std::to_string(result.decoded.sampleRate) +
                              " frames=" + std::to_string(result.decoded.totalSampleFrames) + "\n";
            return result;
        }
        diagnosticLog += "sfx-lookup-debug: bank='" + stem + "' hircItems=" + std::to_string(hircItems.size()) +
                          " mediaRecords=" + std::to_string(mediaRecords.size()) +
                          " kind2=" + std::to_string(kind2Count) + " resolved=" + std::to_string(resolvedCount) +
                          " vorb=" + std::to_string(vorbCount) + " decodeOk=" + std::to_string(decodeOkCount) + "\n";
    }
    diagnosticLog += "sfx-lookup: no kind-2 Sound item in " + archivePath +
                      " both resolved to a media record AND decoded successfully this run\n";
    return result;
}


// Loads the city road + sidewalk graph (spec-zone-data-format.md Sec13.2a: ONE record pair in
// sr3_city~fawld.czn_pc inside awld_compact.str2_pc of sr3_city_0.vpp_pc) from whichever playtest
// archive carries it. The graph is not split per cell on disk; it is parsed once (~2 MB payload) and
// bucketed per Sec10.6(g) tile so the placement code only touches tiles near the player. Returns
// null (synthetic stand-ins stay in use) when no archive carries it.
std::unique_ptr<sr3zone::RoadGraph> LoadPlaytestRoadGraph(const std::vector<std::unique_ptr<CityArchive>>& archives,
                                                           std::vector<sr3zone::HoodVolume>* hoodsOut) {
    const std::string kName = "sr3_city~fawld.czn_pc";
    for (const auto& a : archives) {
        const double t0 = cityNowMs();
        std::vector<uint8_t> czn, sib;
        bool found = false;
        try {
            for (size_t i = 0; i < a->top->entries().size() && !found; ++i) {
                if (a->top->entries()[i].name != "awld_compact.str2_pc") continue;
                vpp::Container nested = a->top->openNested(i);
                found = findEntry(nested, kName, czn, sib, "");
            }
            if (!found) found = findEntry(*a->top, kName, czn, sib, "");
        } catch (const std::exception& ex) {
            std::printf("[roadgraph] %s: container walk failed: %s\n", a->label.c_str(), ex.what());
            continue;
        }
        if (!found) continue;
        try {
            if (hoodsOut) {
                *hoodsOut = sr3zone::LoadHoodVolumes(vpp::ByteView(czn.data(), czn.size()));
                std::printf("[hoods] %zu Hood volumes (hood_bsp_tree + hood_spawn_category) from %s\n", hoodsOut->size(),
                            kName.c_str());
            }
            auto g = std::make_unique<sr3zone::RoadGraph>(sr3zone::RoadGraph::Parse(vpp::ByteView(czn.data(), czn.size())));
            std::printf("[roadgraph] %s :: %s: %zu lanes (segments), %zu road nodes, %zu shape points, "
                        "%zu sidewalk nodes, %zu sidewalk edges; indexed into %zu road tiles / %zu sidewalk tiles; "
                        "%.0f ms (czn %zu bytes)\n",
                        a->label.c_str(), kName.c_str(), g->segments.size(), g->nodes.size(), g->shapePool.size() / 3,
                        g->sidewalkNodes.size(), g->sidewalkEdges.size(), g->TileCountSegments(), g->TileCountSidewalk(),
                        cityNowMs() - t0, czn.size());
            return g;
        } catch (const std::exception& ex) {
            std::printf("[roadgraph] %s: parse FAILED: %s - synthetic lane/sidewalk stand-ins stay in use\n",
                        a->label.c_str(), ex.what());
        }
    }
    std::printf("[roadgraph] no loaded archive carries %s - synthetic lane/sidewalk stand-ins in use\n", kName.c_str());
    return nullptr;
}

} // namespace sr3viewer