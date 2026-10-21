#pragma once

// Shared state for the `sr3_viewer playtest` command (runPlaytest()).  runPlaytest() used to be one
// ~6,900-line function; its locals that are shared between the setup sections, the frame loop and the
// end-of-run report now live in PlaytestSession (setup-time / whole-run state, members in the order the
// old locals were declared) and PlaytestFrame (per-frame locals shared between the frame-loop steps).
// Members that needed a non-trivial constructor/initialiser are std::optional<T> named `<name>_`
// (emplaced at the point the old local was declared); each setup/step function binds `auto& name = ...`
// references at its top so the moved code reads exactly as it did inside runPlaytest().

#include <atomic>
#include <cstdint>
#include <map>
#include <random>
#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "viewer_hud_support.h"
#include "viewer_playtest_support.h"
#include "viewer_playtest_vehicle_rig.h"
#include "viewer_playtest_ambient_models.h"
#include "viewer_skinning.h"
#include "viewer_traffic_demo.h"
#include "viewer_vint_quad.h"
#include "viewer_walk_support.h"
#include "sr3render/device.h"
#include "sr3render/mesh_renderer.h"
#include "sr3render/png_writer.h"
#include "sr3render/quad_renderer.h"
#include "sr3render/sun_shadow_map.h"
#include "sr3render/swap_chain.h"
#include "sr3render/tod_clock.h"
#include "sr3render/tod_lighting.h"
#include "sr3render/weather_schedule.h"
#include "sr3render/window.h"
#include "sr3render/texture_upload.h"
#include "sr3geometry/character_matlib.h"
#include "sr3geometry/geometry_block.h"
#include "sr3geometry/material_block.h"
#include "sr3geometry/material_binding.h"
#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "sr3mesh/mesh_block.h"
#include "sr3rig/animated_pose.h"
#include "sr3rig/bone_palette.h"
#include "sr3rig/pose.h"
#include "sr3rig/rig.h"
#include "sr3player/node_ground.h"
#include "sr3player/jump_crouch.h"
#include "sr3airuntime/abandoned_vehicle_schedule.h"
#include "sr3airuntime/police_response.h"
#include "sr3airuntime/homies.h"
#include "sr3tables_trafficai/tables.h"
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/traffic_yield.h"
#include "sr3airuntime/ped_crossing.h"
#include "sr3activity/activity.h"
#include "sr3collect/challenges.h"
#include "sr3collect/collectibles.h"
#include "sr3activity/escort.h"
#include "sr3activity/fraud.h"
#include "sr3activity/genki.h"
#include "sr3activity/harness.h"
#include "sr3sky/tod_curves.h"
#include "sr3activity/heli.h"
#include "sr3activity/mayhem.h"
#include "sr3activity/snatch.h"
#include "sr3activity/trafficking.h"
#include "sr3hud/dataitem.h"
#include "sr3hud/minimap.h"
#include "sr3hud/display_model.h"
#include "sr3hud/producers.h"
#include "sr3airuntime/ped_navigator.h"
#include "sr3airuntime/traffic_lane_follower.h"
#include "sr3airuntime/road_graph_placement.h"
#include "sr3tables_progression/tables_rules.h"
#include "sr3zone/hood_volumes.h"
#include "sr3zone/road_graph.h"
#include "sr3airuntime/gps_route.h"
#include "sr3zone/zone_heightmap.h"
#include "sr3player/ground_follow.h"
#include "sr3player/vitals.h"
#include "sr3airuntime/ambient_population.h"
#include "sr3airuntime/near_field_vehicle_population.h"
#include "sr3airuntime/freeroam_population.h"
#include "sr3airuntime/police_roadblock_spawn.h"
#include "sr3airuntime/notoriety_escalation.h"
#include "sr3airuntime/roadblock_notoriety_schedule.h"
#include "sr3airuntime/near_field_ped_population.h"
#include "sr3airuntime/npc_ai_runtime.h"
#include "sr3luahost/character_damage_sink.h"
#include "sr3luahost/auto_combat.h"
#include "sr3tables_weapons/weapons.h"
#include "sr3tables_weapons/combat.h"
#include "sr3weapons/weapon_fire.h"
#include "sr3weapons/weapon_inventory.h"
#include "vpp/container.h"
#include "sr3audioruntime/audio_output_device.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3tables_environment/tables.h"
#include "sr3tables_vehicle_world/tables.h"
#include "sr3vintdoc/vint_doc.h"
#include "sr3xtbl/xtbl.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3font/vint_font.h"
#include "sr3font/vint_text.h"
#include "sr3locstr/le_strings.h"
#include "sr3playeranim/bridge.h"
#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"
#include "sr3tables_animation/tables.h"
#include "sr3player/camera_free_seed.h"
#include "sr3player/vehicle_camera.h"
#include "sr3player/character_collision.h"
#include "sr3player/seated_ride_pose.h"
#include "sr3physics/vehicle_substeps.h"
#include "sr3physics/vehicle_driving_slice.h"
#include "sr3physics/vehicle_spec_data.h"
#include "sr3physics/vehicle_world_collision.h"
#include "sr3radio/radio_sim.h"
#include "sr3radio/radio_player.h"
#include "sr3player/ambient_vehicle_entry.h"
#include "viewer_craft_wiring.h"

namespace sr3viewer {

// ---- constants that were `const`/`constexpr` locals of runPlaytest() ----
constexpr int kFootstepCadenceFrames = 18;
const int kScriptObjectiveSetFrame = 2;
const int kScriptObjectiveClearFrame = 150;
const float kVehicleEnterRadius = 3.0f;
const float kVehicleExitOffset = 2.5f;
const float kVehPivotHeight = 1.0f;
const int kScriptVehicleEnterFrame = 10;
constexpr uint64_t kAmbientRigCarId = ~0ull;
constexpr uint64_t kParkedIdBase = 1ull << 62;
// CHOSEN stand-in for the auto-pick max range constant (spec-player-runtime.md Sec13.3 step 3, 0x01115204, no value
// in any spec): the same radius the parked-rig car already used.
const double kAmbientEntryRange = static_cast<double>(kVehicleEnterRadius);
constexpr double kNearFieldDespawnDistanceSq = 10000.0;
constexpr double kNearFieldVehicleTurnRateRadPerSec = 1.4;
constexpr double kNearFieldVehicleCruiseSpeedMs = 6.0;
constexpr double kNearFieldVehicleAccelMs2 = 3.0;
// RF-3 (Sec20.6.3(b)): a_b = vehicle +0xc58 = AI_Max_Braking_Decel x condition scale. Per car, from the car's own model's
// vehicle-info row (AmbientVehicleModel::aiMaxBrakingDecel, nearFieldVehicleModel[id]); condition level 0 is CHOSEN (no
// vehicle damage on ambient cars). kNearFieldVehicleAiMaxBrakingDecel = 12.06 (the car_4dr_standard03 value of the
// Sec20.6.12 test vector) is the CHOSEN FALLBACK only: used on a car's first sim frame (its model is assigned after the
// traffic tick), with the ambient registry off, or when the row value is missing/<=0.
constexpr double kNearFieldVehicleAiMaxBrakingDecel = 12.06;
// RF-2 (Sec20.6.13(f)): CHOSEN stand-in for the per-vehicle mode byte (+0xc68) and obey bit (+0xc6c bit 0): every
// near-field car is an ambient car in mode 8 with the bit set (no chase/flee/rejoin cars exist in this population).
constexpr int kNearFieldVehicleMode = sr3airuntime::kVehicleModeAmbient;
constexpr bool kNearFieldVehicleObeysLights = true;
constexpr double kNearFieldVehicleHalfLength = 2.2;
constexpr double kNearFieldVehicleProbeHalfWidth = 2.0;
constexpr double kSignalResidentRadius = 400.0;
constexpr double kSignalLogRadius = 150.0;
constexpr uint64_t kSignalMemoryId = ~0ull - 2;
constexpr uint64_t kStopLineMemoryId = ~0ull - 3;  // TF-7: the kind-4 (stop-line) source's memory entry id
constexpr int64_t kNearFieldPedTickMinMs = 200;
constexpr int64_t kNearFieldPedTickMaxMs = 300;
const int kScriptRoadblockArmFrame = 20;
const int kScriptRoadblockDisarmFrame = 280;
const int kScriptFireFrame = 60;

// ---- local helper types of runPlaytest(), moved here verbatim ----
struct PlaytestRootMotion : sr3playeranim::ClipRootMotionSource {
    sr3player::Vec3 translationPerSecond(int32_t, bool loop) override {
        // SYNTHETIC stand-in, same precedent as runWalkDemo()'s own WalkRootMotion -
        // no real per-clip root motion exists anywhere in this project.
        return loop ? sr3player::Vec3{0, 0, 2.5} : sr3player::Vec3{};
    }
};

struct PlaytestMoveSource : sr3player::MoveInputSource {
    float A = 0.0f, B = 0.0f;
    bool sprintHeld = false;
    bool crouchHeld = false;  // T2: C key (CBA_OFC_CROUCH, keyboard hold mode, spec-player-runtime.md Sec3.6.3 step 2)
    float walkAxisA() override { return A; }
    float walkAxisB() override { return B; }
    bool gamepadActive() override { return false; }  // keyboard+mouse only, see this command's own scope note
    bool held(sr3player::MoveAction a, int* out) override {
        if (a == sr3player::MoveAction::Crouch && crouchHeld) {
            if (out) *out = sr3player::kOutKeyOrMouse;  // keyboard source -> hold mode (Sec3.6.3 step 2)
            return true;
        }
        return a == sr3player::MoveAction::Sprint && sprintHeld;
    }
    // JC-5 (Sec3.6.3 step 3): CBA_OFC_CROUCH_TOGGLE pressed on keyboard/mouse flips the crouch latch. The key
    // ('X') is CHOSEN (the spec gives no default binding); set for exactly one frame on the key edge.
    bool crouchTogglePressed = false;
    bool pressed(sr3player::MoveAction a, int* out) override {
        if (a == sr3player::MoveAction::CrouchToggle && crouchTogglePressed) {
            if (out) *out = sr3player::kOutKeyOrMouse;  // keyboard source -> toggle on CROUCH_TOGGLE (step 3)
            return true;
        }
        return false;
    }
    void consume(sr3player::MoveAction a) override {
        if (a == sr3player::MoveAction::CrouchToggle) crouchTogglePressed = false;
    }
    bool optionBit0() override { return true; }
};

struct PlaytestStamina : sr3player::SprintStamina {
    // Same always-available stand-in runWalkDemo()'s own WalkScriptStamina uses -
    // this project has no real stamina state machine (metered_sprint.xtbl's
    // parameters are specified, its transitions are not, spec-player-runtime.md
    // Sec3.3/Sec3.6.2).
    void notSprintingStep(sr3player::PlayerMovementState&) override {}
    bool sprintingStep(sr3player::PlayerMovementState&) override { return true; }
    double available() override { return 1.0; }
    void startPenalty() override {}
    void jumpPenalty() override {}
    void step9ff270() override {}
};

struct PlaytestLookSource : sr3player::LookInputSource {
    float rotateValue = 0.0f, elevateValue = 0.0f;
    float rotate() override { return rotateValue; }
    float elevate() override { return elevateValue; }
    bool cameraDrivenByKbm() override { return true; }  // keyboard+mouse only, no gamepad path here
    bool invertHorizontal() override { return false; }
    bool invertVertical() override { return false; }
};

struct RoadGraphGlobalReset {
    ~RoadGraphGlobalReset() {
        g_playtestRoadGraph = nullptr;
        g_playtestGps = nullptr;
    }
};

struct AutoRoute {
    bool loaded = false;
    std::vector<std::string> triggers;   // lowercase, route order (go-to-trigger + drive-to rows)
    std::vector<int> triggerRow;         // route step number of each entry (alternatives share a row)
    std::unordered_map<std::string, int> altBlockedUntilFrame; // lowercase name -> frame
    std::unordered_set<std::string> hazards; // lowercase
    std::vector<std::string> vehicles;   // named enter-vehicle rows (original case)
};

// ---- state shared by the setup sections, the frame loop and the end-of-run report ----
// One placed object of an activity instance's own zone (`<instance>.str2_pc`), viewer_playtest_activity.cpp.
struct ActivityZoneObject {
    std::string name;       // ctg_object_name
    std::string className;  // class hash name, or the hash in hex
    bool hasPos = false;
    double x = 0.0, y = 0.0, z = 0.0;
};

// An armed activity instance: its start point (CHOSEN: the Warp_Location navpoint, see
// sr3activity::AtActivityStart) - walking into it begins the instance.
struct ActivityMarker {
    std::string name;      // instance, e.g. _a_sn_ne_01
    std::string warpName;  // Warp_Location
    sr3activity::ActivityPos start;
};

struct PlaytestSession;
struct PlaytestFrame;

// Watchdog beacons (first-playable stability pass): the frame loop publishes the step it is in and a millisecond tick on
// every frame; a host-side watchdog thread (tools/sr3_game/main.cpp) reports + aborts when the tick stops advancing.
extern std::atomic<const char*> g_curStep;
extern std::atomic<unsigned long long> g_lastTickMs;

// --script-soak=SEED chaos driver (viewer_playtest_soak.cpp): deterministic random player behaviour + heartbeat log.
struct SoakState {
    bool active = false;
    uint32_t seed = 0;
    int round = 0;
    int heartbeatEvery = 300;
    std::mt19937 rng;
    bool seeded = false;
    int actKind = -1, actStart = 0, actEnd = 0;   // current action [actStart, actEnd)
    int subPhase = 0;
    int driveFrames = 0;
    int driveNextChange = 0;
    double driveThrottle = 0, driveBrake = 0, driveSteer = 0;
    std::string craftName;
    long long hbWallLast = 0;                     // QPC ticks at the previous heartbeat
    int hbFrameLast = 0;
    double peakWsMB = 0, peakPrivMB = 0;
    uint64_t actionCounts[24] = {};
};

// FIRST PLAYABLE (sr3.exe) extension points. Every member defaults to empty, in which case the playtest
// behaves exactly as before (sr3_viewer playtest passes none). tools/sr3_game/main.cpp installs them to
// run the front end (main menu before the world is built; the pause menu inside the live loop).
enum class PauseRequest { None, Toggle, Open, Close, Quit };
struct PlaytestHooks {
    // Right after the window/device exist (before any heavy asset work): lets the host paint a first frame.
    std::function<void(PlaytestSession&)> afterWindow;
    // After the HUD/font/pipeline exist, before the world (and --load-save) is applied. -1 = continue, else exit code.
    std::function<int(PlaytestSession&)> beforeWorld;
    // Every frame at the top of stepInput. May request pause open/close/toggle or quit; may do work (e.g. save).
    std::function<PauseRequest(PlaytestSession&, PlaytestFrame&)> pauseInput;
    // In the HUD pass while the pause menu is open (after the dim rect). When set, replaces the default
    // "PAUSED / Resume: Esc / Quit: F10" labels.
    std::function<void(PlaytestSession&)> pauseDraw;
    // After the loop, before --save-at-exit and the report.
    std::function<void(PlaytestSession&)> afterLoop;
};

inline std::atomic<uint64_t> g_playtestSessionCounter{0};

struct PlaytestSession {
    PlaytestHooks hooks;
    // Unique per session object (sr3.exe builds a fresh session for every Load / New Game from the main menu). Any
    // function-level or file-level cache that holds pointers into session-owned objects MUST be keyed/cleared by this id:
    // the freed objects' addresses are reused by the next session (stability pass: a police model pointer cached in a
    // function-level static dangled after the first Load and crashed stepRender).
    const uint64_t sessionId = ++g_playtestSessionCounter;
    bool playerTargetSeeded = false;  // stepPlayerView: playerGroundTarget seeded from the follower (once per session)

    // --- setupArgs (original lines 443-965) ---
    std::string charactersPath;
    std::string miscTablesPath;
    std::vector<std::string> cityArchivePaths;
    std::vector<std::string> luaPreloadArchivePaths;
    bool headless = false;  // --headless: forces the window hidden (ctest use)
    bool noInput = false;  // --no-input: scripted fallback, see this function's doc comment
    int maxFrames = -1;  // -1 = run until the window closes/Esc (interactive default)
    std::string capturePath;
    int captureFrame = -1;
    std::vector<int> captureFrames;
    uint32_t winWidth = 1600;
    uint32_t winHeight = 900;
    float spawnX = -1280.0f;
    float spawnZ = -560.0f;
    bool spawnYOverrideGiven = false;  // --spawn-y, see its own doc comment below
    float spawnYOverride = 0.0f;
    bool autoObjective = false;
    bool autoCombat = false;
    int autoObjectiveFrames = 80;
    bool autoSeatMissionVehicle = false;
    bool autoSkipCutscenes = false;
    float farZ = 3000.0f;
    std::string animArchiveOverride;
    float mouseSensitivity = 1.0f;
    int scriptForwardFrames = 200;
    int scriptTurnFrames = 60;  // --no-input fallback shape, see below
    bool hudEnabled = false;
    std::string hudRegistrationListPath;
    std::string hudFontArchivePath;
    bool missionEnabled = false;
    std::string missionStem;
    std::string missionRegistrationListPath;
    std::string missionPackageTsvPathOverride;
    double missionTimeoutSeconds{};
    bool vehicleEnabled = true;
    bool noAmbientModels = false;  // --no-ambient-models: ambient cars/peds keep the single placeholder model
    bool noPedAnim = false;        // --no-ped-anim: ambient peds keep the static bind pose (A/B for the walk animation)
    std::string vehicleArchiveOverride;
    std::string vehicleWheelsArchiveOverride;
    std::string interfaceArchiveOverride;
    std::string vehicleCmeshName = "car_4dr_genki_0.ccar_pc";  // same CHOSEN representative vehicle as runDriveDemo()
    std::string vehicleInteractionNameOverride;
    float vehicleSpawnX = 0.0f;
    float vehicleSpawnZ = 0.0f;  // resolved relative to --spawn below once parsing is done
    bool scriptVehicle = false;  // --script-vehicle: deterministic enter/drive/exit under --no-input (smoke test)
    int scriptVehicleDriveFramesOverride = -1;  // -1 = use the existing default (120)
    bool noCollision = false;
    // CHOSEN ground stand-in switch (results/freeroam_ground_height_20261018.md): --no-node-ground restores the old behaviour.
    bool noNodeGround = false;
    sr3player::NodeGroundIndex nodeGround;
    bool noNpcAi = false;
    bool npcAiNoFire = false;
    // Homies and followers (feature/homies; sr3airuntime/homies.h, spec-ai-runtime.md Sec24). All opt-in debug inputs:
    bool noHomies = false;                                 // --no-homies
    struct HomieCallRequest { std::string name; int frame = 45; };
    std::vector<HomieCallRequest> homieCalls;              // --call-homie=NAME[@FRAME]
    std::vector<int> homieDismissOneAt, homieDismissAllAt; // --dismiss-homie-at=F / --dismiss-all-homies-at=F
    int homieSlots = 1;                                    // --homie-slots=N (HomieSlot1..3 unlocked in order; new game: 1)
    bool homieAllowMax = false;                            // --homie-allow-max (party_allow_max_followers(true))
    bool homieNotRecruitable = false;                      // --homie-not-recruitable (party_set_recruitable(false))
    std::vector<std::string> homieUnlock;                  // --homie-unlock=NAME|all (stands in for the Homie unlockable being set)
    bool homieAllCanLeave = false;                         // --homie-can-leave: every follower is can_leave_behind (OPEN default)
    int homieInjureAt = -1;                                // --homie-injure-at=F: the first follower drops to 0 hp (downed type 0)
    struct HomieFriendlyFire { int frame = -1; double damage = 0.0; };
    std::vector<HomieFriendlyFire> homieFriendlyFire;      // --homie-friendly-fire=F:DMG (the player hits the first follower)
    struct HomieWanted { int frame = -1; int level = 0; };
    std::vector<HomieWanted> homieWantedAt;                // --homie-wanted-at=F:LEVEL: set the police track at frame F (evidence harness)
    bool noPoliceResponse = false;  // --no-police-response: no notoriety-driven police units (A/B)
    // GPS (spec-ai-runtime.md Sec23): --gps-waypoint=x,z [--gps-waypoint-at=FRAME] [--gps-waypoint-mission]
    bool gpsWaypointFlag = false;
    double gpsWaypointX = 0.0, gpsWaypointZ = 0.0;
    int gpsWaypointAt = 30;
    bool gpsWaypointMission = false;
    bool gpsWaypointIssued = false;
    std::optional<sr3airuntime::gps::GpsGraph> gpsGraph_;
    std::optional<sr3airuntime::gps::GpsPlanner> gps_;
    uint64_t gpsLuaAdds = 0, gpsLuaRemoves = 0, gpsLuaUnresolved = 0;
    int gpsLastVehicleClass = -1;  // Sec23.2 step 1 "current or last vehicle": Vehicle_Type class last driven (-1 none)
    double gpsMinDistToGoal = 1.0e30;
    int debugNotoriety = -1;
    int debugDamagePlayer = -1;  // --debug-damage-player=N: DEBUG damage of N applied to the player at frame 30 (T1)
    double debugPlayerCashDollars = -1.0;  // --debug-player-cash=D: starting player cash in dollars (verification of the Sec18.7 penalty)
    bool scriptRoadblock = false;
    bool scriptFire = false;
    // T2 jump/crouch: --script-jump-at=FRAME (repeatable), --script-crouch-frames=A-B (inclusive), --jump-height=M (CHOSEN override)
    std::vector<int> scriptJumpAt;
    SoakState soak;  // --script-soak=SEED [--soak-round=N] [--soak-heartbeat=N]
    int scriptCrouchA = -1, scriptCrouchB = -1;
    std::vector<int> scriptCrouchToggleAt;  // --script-crouch-toggle-at=FRAME (repeatable): JC-5 toggle-mode press
    bool prevCrouchToggleKeyDown = false;
    double jumpHeight = sr3player::kDefaultJumpHeightMetres;
    std::optional<sr3player::JumpController> jump_;
    bool prevSpaceDown = false;
    bool scriptHudPromptResponse = false;
    bool autoAnswerHudPrompts = false;
    int hudPromptAutoAnswerDelayFrames = 15;  // CHOSEN default (~0.5s at the --no-input fixed 1/30s step)
    bool noAudio = false;
    std::string sfxArchiveOverride;
    bool visible{};

    // --- setupBrad (original lines 967-1106) ---
    std::optional<MappedBytes> charArchiveBytes_;
    std::vector<uint8_t> cb;
    std::vector<uint8_t> gb;
    std::vector<uint8_t> rb;
    std::vector<uint8_t> unusedSibling;
    std::vector<uint8_t> headCb;
    std::vector<uint8_t> headGb;
    std::optional<sr3geometry::MaterialBlock> material_;
    std::optional<sr3geometry::GeometryBlock> geometry_;
    std::optional<sr3mesh::MeshBlock> mesh_;
    size_t channelIndex = 0;
    size_t drawGroup = 0;
    std::optional<sr3rig::Rig> rig_;
    const std::vector<sr3rig::Bone>* bones_p{};
    std::optional<SkinningModeResult> skinModeResult_;
    bool useBonePalette{};
    std::vector<uint32_t> parentIdx;
    std::vector<std::array<float, 3>> restMesh;
    std::vector<std::array<float, 3>> restRig;
    std::vector<sr3rig::Mat3x4> bindSkin;
    std::vector<sr3mesh::Vertex> verts;
    std::optional<sr3render::VertexDuplication> vertexDuplication_;
    std::optional<sr3rig::SkinningStats> bindStats_;
    std::vector<std::array<float, 3>> bindPositions;
    std::optional<sr3geometry::MaterialBlock> headMaterial_;
    std::optional<sr3geometry::GeometryBlock> headGeometry_;
    std::optional<sr3mesh::MeshBlock> headMesh_;
    size_t headChannelIndex = 0;
    size_t headDrawGroup = 0;
    std::optional<SkinningModeResult> headSkinModeResult_;
    std::vector<std::array<float, 3>> headRestMesh;
    std::vector<sr3rig::Mat3x4> headBindSkin;
    std::vector<sr3mesh::Vertex> headVerts;
    std::optional<sr3render::VertexDuplication> headVertexDuplication_;
    std::vector<std::array<float, 3>> headBindPositions;

    // --- setupTables (original lines 1108-1348) ---
    std::optional<MappedBytes> miscBytes_;
    std::optional<vpp::Container> miscTop_;
    std::function<std::optional<std::string>(const std::string&)> miscRead;
    std::function<sr3xtbl::Document(const std::string&)> docOf;
    std::vector<sr3tables_environment::WeatherTimeSegment> todSegments;
    // Weather (gap #9): the stage is driven by sr3render::WeatherScheduler (spec-tables-environment Sec18) and feeds
    // BOTH the TOD lighting blend and the ped density. --weather=<stage> pins it; the default is the schedule.
    std::string weatherPinName;                              // empty = scheduled
    uint32_t weatherSeed = sr3render::kDefaultWeatherSeed;   // CHOSEN RNG seed (--weather-seed=)
    double weatherTimeScale = 1.0;                           // CHOSEN test knob: scheduler s per sim s (--weather-time-scale=)
    double todSecondsPerDay = sr3render::kTodSecondsPerDay;  // --tod-seconds-per-day= (accelerated runs)
    std::optional<sr3render::WeatherScheduler> weatherSched; // built in setup once weather.xtbl is read
    std::string weatherLightA, weatherLightB;                // resolved lighting stage names of the blend corners
    float weatherLightBlend = 0.0f;
    int weatherTraceFrames = 600;  // periodic blend trace interval (frames); stage changes always trace
    double weatherSimSeconds = 0.0;
    std::unordered_map<std::string, sr3airuntime::SpawnCategoryDensity> spawnCategoryDensity;
    std::vector<sr3zone::HoodVolume> playtestHoods;  // filled by LoadPlaytestRoadGraph() once the city archives are open
    float freeroamWeatherPedDensity = 1.0f;
    std::string freeroamHoodName = "(none)";
    std::string freeroamCategoryName = "Default Spawn";
    std::function<sr3airuntime::SpawnCategoryDensity(const sr3airuntime::Vec3&)> currentSpawnCategory;
    std::vector<std::string> inventoryItemNames;  // items_inventory.xtbl (+DLC) names -> inv_item_remove resolver
    std::vector<std::string> items3dNames;  // items_3d.xtbl row names -> character_add_prop resolver
    std::vector<std::string> weaponDefNames;  // every weapons.xtbl (+DLC) row Name -> inv_weapon_*_temporary resolver
    std::vector<std::pair<std::string, int32_t>> weaponDefClasses;  // same rows, Weapon_Class (Sec55.4 thrown-class rule)
    std::optional<sr3tables_weapons::Weapon> bradWeaponDef_;
    bool bradWeaponDefFound = false;
    std::shared_ptr<const sr3airuntime::NpcAiTables> npcAiTables;
    std::unique_ptr<sr3weapons::WeaponInstance> bradWeapon;  // mission harness auto-combat weapon (infinite ammo)
    // Gap #17: the player's on-foot inventory (clip/reserve/reload/switch). Null if weapons.xtbl is unavailable.
    std::unique_ptr<sr3weapons::WeaponInventory> weaponInv;
    bool scriptWeapons = false;      // --script-weapons: scripted fire-until-empty/reload/switch timeline
    bool prevReloadDown = false;
    uint32_t prevWeaponSelKeys = 0;  // edge state: bits 0-8 = keys 1-9, bit 9 = Q (prev), bit 10 = Tab (next)
    uint64_t weaponTraceLastFrame = 0;

    // --- setupAnimRuntime (original lines 1350-1499) ---
    std::optional<sr3xtbl::Document> paramsDoc_;
    std::optional<sr3animruntime::ControlParameterRegistry> registry_;
    std::optional<sr3xtbl::Document> filtersDoc_;
    std::optional<sr3animruntime::ControlFilterTable> filters_;
    std::vector<std::string> states;
    std::vector<std::string> actions;
    std::vector<std::string> triggerNames;
    std::optional<sr3animruntime::StateActionRegistry> stateActions_;
    std::optional<sr3animruntime::TriggerRegistry> triggers_;
    std::vector<int32_t> walkLeftFootFrames;
    std::vector<int32_t> walkRightFootFrames;
    bool walkTriggerFramesFound = false;
    std::string footstepEventClipFilename;
    std::optional<sr3xtbl::Document> listDoc_;
    std::optional<sr3animruntime::NodeGraphLoadInputs> loadIn_;
    std::optional<sr3animruntime::NodeGraphLibrary> lib_;
    std::optional<sr3animruntime::GraphInstancePool> pool_;
    std::optional<sr3playeranim::PlayerGraphServices> svc_;
    PlaytestRootMotion motion;
    std::optional<sr3playeranim::PlayerAnimBridge> bridge_;

    // --- setupPoseAndInput (original lines 1501-1679) ---
    std::optional<RealClipRegistry> clipRegistry_;
    std::unique_ptr<MappedBytes> animArchiveBytes;
    int32_t walkClipDurationUnits = 0;
    std::optional<sr3animpose::LoopPhaseTracker> loopPhase_;
    bool realPoseEnabled{};
    float prevWalkTriggerPhase = 0.0f;  // footstep-real-event: last frame's loopPhase.phaseUnit(), for crossing detection
    std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationSet> rideSets;
    std::optional<sr3tables_vehicle_world::VehicleInteractionInfo> demoVehicleInteractionInfo_;
    bool demoVehicleInteractionInfoFound = false;
    std::unordered_map<std::string, int32_t> rideClipRecords;
    std::optional<sr3player::SeatedRidePoseApplier> seatedPoseApplier_;
    double seatedPoseClockSeconds = 0.0;
    PlaytestMoveSource moveSrc;
    PlaytestStamina stamina;
    std::optional<sr3player::PlayerMovement> pm_;
    sr3player::Vec3 heldFacing{0, 0, 1};
    PlaytestLookSource lookSrc;
    std::optional<sr3player::CameraSystem> followCam_;

    // --- setupCityArchives (original lines 1681-1734) ---
    std::vector<std::unique_ptr<CityArchive>> archives;
    std::vector<CityCell> cells;
    std::map<std::string, size_t> cellByName;
    std::unique_ptr<sr3zone::RoadGraph> playtestRoadGraph;
    std::optional<RoadGraphGlobalReset> roadGraphGlobalReset_;

    // --- setupWindowDevice (original lines 1736-1761) ---
    std::string error;
    std::optional<sr3render::Window> window_;
    std::optional<sr3render::RenderDevice> device_;

    // --- setupAudio (original lines 1763-1896) ---
    std::optional<sr3audioruntime::AudioOutputDevice> audioDevice_;
    bool audioReady = false;
    std::optional<sr3radio::RadioSystem> radio_;
    std::optional<sr3radio::XtblArchive> radioXtbl_;
    std::optional<sr3radio::RadioMedia> radioMedia_;
    std::unique_ptr<sr3radio::RadioPlayer> radioPlayer;
    int radioLastStation = 0xFF;  // per-vehicle +0x1630 equivalent (one vehicle in this command)
    std::optional<FootstepCadenceSfxClip> footstepClip_;
    int framesSinceLastFootstep = 0;
    // Weapon fire sound clip (T6). CHOSEN: left invalid - the weapon fire/dry-fire Wwise event name -> media mapping is
    // not specified (Sec2.2c gives only hashed ids 0x50d893ff/0x5ae33e86), so nothing is played; plumbing plays once set.
    FootstepCadenceSfxClip weaponFireClip;
    uint64_t weaponFireSoundsPlayed = 0, weaponFireSoundsUnresolved = 0;
    uint64_t footstepsTriggered = 0;

    // --- setupRenderAssets (original lines 1898-2079) ---
    std::optional<sr3render::SwapChain> swapChain_;
    std::optional<sr3render::MeshRenderer> focusBox_;
    uint32_t depthWidth{};
    uint32_t depthHeight{};
    std::optional<sr3render::SunShadowMap> sunShadowMap_;
    std::string shadowMapError;
    bool shadowMapReady{};
    std::optional<sr3render::MeshRenderer> characterRenderer_;
    const sr3render::VertexDuplication* dupPtr{};
    std::optional<sr3render::MeshRenderer> characterHeadRenderer_;
    std::vector<sr3render::UploadedTexture> characterMaterialTextures;
    std::vector<sr3render::UploadedTexture> characterHeadMaterialTextures;
    std::vector<ID3D11ShaderResourceView*> characterPerMaterialSrv;
    std::vector<ID3D11ShaderResourceView*> characterHeadPerMaterialSrv;
    bool characterTextured = false;
    bool characterHeadTextured = false;

    // --- setupHudAndMission (original lines 2081-2733) ---
    std::unique_ptr<sr3luahost::Host> hudHost;
    sr3luahost::EngineState* hudEs = nullptr;
    lua_State* hudGp = nullptr;
    std::optional<HudDemoFont> hudFont_;
    std::optional<sr3locstr::LeStringsLookup> locLookup_;
    std::optional<VintQuadPipeline> hudPipe_;
    uint32_t hudHealthH = 0;
    uint32_t hudAmmoH = 0;
    uint32_t hudMoneyH = 0;
    uint32_t hudNotorietyH = 0;
    uint32_t hudVehiclePromptH = 0;
    std::array<uint32_t, 4> hudObjectiveH{0, 0, 0, 0};
    double hudMaxHp = 0.0;
    // sr3hud wiring (Team C, HANDOFF-C.md section 6 step 2): the core HUD dataitems, their
    // producers and the HUD scripts' display model drive the labels below.
    uint32_t hudRespectH = 0;
    sr3hud::HudDataItems hudItems;
    sr3hud::HealthDisplay hudHealthDisplay;
    sr3hud::CashDisplay hudCashDisplay;
    sr3hud::NotorietyDisplay hudNotorietyDisplay;
    sr3hud::RespectDisplay hudRespectDisplay;
    sr3hud::RespectProducer hudRespectProducer;
    std::string hudRespectGainText;  // "+N" of the latest gain
    // sr3hud minimap (HANDOFF-C.md section 6 step 3): speed zoom state, the committed camera
    // forward (written each frame by the player step), and the minimap_CCRR textures by tile.
    sr3hud::MinimapSpeedZoom minimapZoom;
    double minimapCamFwdX = 0.0, minimapCamFwdZ = 1.0;
    std::map<std::pair<int, int>, sr3render::UploadedTexture> minimapTileTex;
    std::set<std::pair<int, int>> minimapTileTried;
    // Save/load (HANDOFF-C.md section 6 step 5, spec-save-format.md A.1/A.2).
    std::string loadSavePath;    // --load-save <sr3save_NN.sr3s_pc>
    std::string saveAtExitPath;  // --save-at-exit <file>
    bool writePositionSidecar = false;  // sr3.exe: also write/read <save>.sr3pos (CHOSEN, exact player position)
    std::string savedPositionNote;
    std::vector<uint8_t> loadedSaveImage;
    // Activities (Team C, viewer_playtest_activity.cpp): --activity <instance>; Insurance Fraud so far.
    std::string activityName;
    double fraudTestHitEveryS = 0.0;  // --fraud-test-hit-every S (test aid: synthetic hits)
    double fraudTestHitClockS = 0.0;
    std::optional<sr3activity::ActivityInstance> activityInstance;
    std::optional<sr3activity::FraudRun> fraudRun;
    std::optional<sr3activity::SnatchRun> snatchRun;
    std::optional<sr3activity::MayhemRun> mayhemRun;
    std::optional<sr3activity::EscortRun> escortRun;
    std::optional<sr3activity::TraffickingRun> traffickingRun;
    std::optional<sr3activity::GenkiRun> genkiRun;
    std::optional<sr3activity::HeliRun> heliRun;
    double heliTestObjectiveEveryS = 0.0;  // --heli-test-objective-every S (test aid)
    double heliTestObjectiveClockS = 0.0;
    double genkiTestKillEveryS = 0.0;  // --genki-test-kill-every S (test aid)
    double genkiTestKillClockS = 0.0;
    double traffickingTestDealEveryS = 0.0;  // --trafficking-test-deal-every S (test aid)
    double traffickingTestDealClockS = 0.0;
    bool escortTestDrive = false;     // --escort-test-drive (test aid)
    bool escortEnteredVehicle = false;  // the abandon countdown starts after the first entry (CHOSEN)
    double mayhemTestKillEveryS = 0.0;  // --mayhem-test-kill-every S (test aid)
    double mayhemTestKillClockS = 0.0;
    double snatchTestDeliverEveryS = 0.0;  // --snatch-test-deliver-every S (test aid)
    double snatchTestDeliverClockS = 0.0;
    sr3activity::ActivitySaveRecord activitySave;
    std::vector<ActivityZoneObject> activityObjects;  // the instance zone's placed objects
    bool activityListObjects = false;                 // --activity-list-objects
    bool activitiesAll = false;                       // --activities: arm every shipped instance
    std::vector<ActivityMarker> activityMarkers;
    int activityInsideMarker = -1;  // start points trigger on entry, not while standing in them
    bool activityRunning = false;
    // --auto-activity: the end-to-end harness (CHOSEN stand-ins, sr3activity/harness.h).
    bool autoActivity = false;
    bool autoActivityTeleported = false;
    std::optional<sr3activity::ActivityPos> autoPos;
    std::optional<sr3activity::FraudStrikeHarness> fraudStrikes;
    std::optional<sr3activity::SnatchHostages> snatchHostages;
    sr3activity::SnatchLayout snatchLayout;
    std::optional<sr3activity::ActivityPos> snatchDropoff;
    // Activity HUD bars (0..1, < 0 = hidden) and the totals they are measured against.
    double activityMeter = -1.0, activityTimer = -1.0;
    double activityTimerTotalS = 0.0, activityMeterTotal = 0.0;
    // Collectibles and challenges (Team C, viewer_playtest_collect.cpp).
    std::array<sr3collect::CollectibleGroupDef, sr3collect::kCollectibleGroups> collectDefs;
    sr3collect::CollectibleRegistry collectibles;
    std::optional<sr3collect::ChallengeTracker> challenges;
    std::optional<sr3collect::CollectResult> pendingCollect;  // the collection screen being shown
    std::vector<int> challengePayQueue;                       // completed, paid when the banner ends
    double collectBannerS = 0.0;
    uint32_t hudCollectH = 0;
    double collectTestPickupEveryS = 0.0;  // --collectible-test-pickup-every S (test aid)
    double collectTestClockS = 0.0;
    int collectTestNext = 0;
    bool challengesUnlockAll = false;  // --challenges-unlock-all (test aid)
    bool collectHavePrev = false;
    double collectPrevX = 0.0, collectPrevZ = 0.0;
    bool achievedGettingTheGoods = false;
    // Base TOD curves, default_district.xtbl (Team C, viewer_playtest_sky.cpp; loaded on first use).
    std::optional<sr3sky::TodBaseCurves> todBaseCurves;
    bool todBaseCurvesTried = false;
    // Real HUD sheet art (viewer_playtest_hudart.cpp): page + sub-rect per image name, looked up once
    // (srv null = not found, flat fallback).
    struct HudArtImage { ID3D11ShaderResourceView* srv = nullptr; uint32_t texW = 0, texH = 0; int x = 0, y = 0, w = 0, h = 0; };
    std::map<std::string, HudArtImage> hudArtImages;
    double collectDrivenM = 0.0;
    bool collectDriveLogged = false;
    uint32_t hudActivityH = 0;
    bool activityDone = false;
    std::vector<std::unique_ptr<CityArchive>> hudFontArchivesOwned;
    bool missionDriveActive = false;  // true once bound and not yet finished/errored/timed out
    uint16_t missionStartRecordId = sr3luahost::BareThreadTable::kNoThread;
    bool missionStartTracked = false;  // true while _start's own record is still being followed
    std::string missionScriptEntryName;
    double missionElapsedSeconds = 0.0;  // accumulated against this loop's own per-frame dt - see this
    size_t hudPromptRequestsRespondedCount = 0;
    std::vector<int> hudPromptFirstSeenFrame;
    bool pauseMenuOpen = false;
    bool prevEscDown = false;
    bool prevF10Down = false;
    bool pauseDocLoadAttempted = false;  // real pause_menu_common.vint_doc - tried at most once
    bool pauseDocLoaded = false;
    uint32_t pauseDocHandle = 0;
    uint32_t pauseTitleH = 0;
    uint32_t pauseResumeH = 0;
    uint32_t pauseQuitH = 0;  // synthetic overlay labels (see below)
    uint64_t acNpcToAttack = 0;
    uint64_t acNpcToCover = 0;
    uint64_t acPlayerShots = 0;
    uint64_t acPlayerHits = 0;
    uint64_t acTeleports = 0;
    uint64_t acHeals = 0;
    uint64_t acKillsDamage = 0;
    uint64_t acKillsFallback = 0;
    uint64_t acHostileOrders = 0;

    // --- setupNpcAiState (original lines 2735-2830) ---
    std::optional<sr3luahost::EngineState> standaloneCombatEs_;
    sr3luahost::EngineState* combatEs_p{};
    std::optional<sr3luahost::EngineStateLethalDamageSink> bradDamageSink_;
    std::function<int()> liveNotorietyLevel;
    // T5 (2026-10-18): the session notoriety state (spec-ai-runtime.md Sec19.8 recipe, notoriety_escalation.h). It
    // owns the alias tracks unless --debug-notoriety=N is given (override wins: the driver then never writes them).
    std::optional<sr3airuntime::NotorietyEscalation> notoriety_;
    std::array<double, 4> notorietyAliasLastWritten{0.0, 0.0, 0.0, 0.0};
    // T1 (player vitals, spec-player-runtime.md Sec15.3a/Sec15.4): free-roam only; mission runs keep the mission death path.
    std::optional<sr3player::PlayerVitals> vitals_;
    bool vitalsInit = false;
    // Sec18 (2026-10-18) death-sequence / regen bookkeeping (free roam):
    bool vitalsOnDeathFired = false;
    bool vitalsRegenActive = false;
    int64_t vitalsLastRegenLogMs = 0;
    int32_t vitalsPrevHp = -1;
    int64_t vitalsHospitalBillsCents = 0;  // profile stat 4 "hospital bills" (Sec18.7), accumulated in cents
    double vitalsFadeAlpha = 0.0;           // 0..1 black level of the death fade-out (no overlay is drawn: logged)
    // spec-player-runtime.md Sec18.10-18.16 additions.
    bool bustEnabled = false;            // --enable-bust: Sec18.10 says retail never enters AI mode 4; the port keeps it opt-in
    bool bustPending = false;            // set by stepPolice when the Sec18.10 gates pass for a cop within 5 m
    bool bustCuffing = false;            // player in action state (7,10)
    sr3player::CuffState bustCuff;
    double bustCuffCarry = 0.0;
    int64_t vitalsArrests = 0;           // profile stat 11
    int64_t vitalsBailCents = 0;         // profile stat 5 "bail money"
    int64_t vitalsFadeInStartMs = -1, vitalsFadeInEndMs = -1;  // Sec18.12 wipe window (value only, no overlay)
    int debugHealthTier = -1;            // --debug-health-tier=N (1..4): apply Player_Health_N at frame 5 (Sec18.14)
    double debugBustRangeM = -1.0;       // --debug-bust-range=M: DEBUG evidence only, replaces the 5 m approach distance
    std::string vitalsTrace;  // accumulated player-vitals-trace event lines (printed at exit)
    int debugNotorietyPoints = -1;  // --debug-notoriety-points=N: DEBUG crime of N police points at frame 1
    uint64_t crimeReports = 0;
    uint64_t notorietyLevelChanges = 0;
    std::unordered_set<uint64_t> vehicleHitPedIds;  // peds already reported as hit by the player's vehicle
    // Gap #8 vehicle collisions (viewer_playtest_frame_collision.cpp). All thresholds/masses are CHOSEN there.
    struct VehicleKnock {  // an ambient car knocked off its lane follower: free-sliding until it stops, then recovers
        double vx = 0.0, vz = 0.0;
        double stoppedMs = 0.0;
    };
    // TEST-ONLY (--script-collision-targets=FRAME): at that frame, while driving, inject a frozen target ped (10 m ahead),
    // a second one (18 m, slightly right) and a stationary ambient car (24 m ahead) in the car's path.
    int scriptCollisionTargetsAt = -1;
    std::unordered_map<uint64_t, VehicleKnock> vehicleKnock;
    std::unordered_map<uint64_t, double> pedKnockedUntilMs;  // ped id -> nowMs the knockdown ends (frozen meanwhile)
    std::unordered_set<uint64_t> pedKilledByVehicle;         // ped ids run over lethally (stay down)
    std::unordered_map<uint64_t, double> pedContactCooldownMs;  // per-ped re-hit lockout (one report per contact)
    std::unordered_map<uint64_t, double> carPairCooldownMs;     // per ambient car re-contact lockout
    std::unordered_map<uint64_t, int> vehicleDamage;            // car id (kAmbientRigCarId = player rig) -> damage total
    struct VehicleCollisionStats {
        uint64_t pedContacts = 0, pedBumps = 0, pedKnockdowns = 0, pedKills = 0;
        uint64_t carCarContacts = 0, carStaticContacts = 0;
        double maxImpactSpeed = 0.0;      // m/s closing speed, any contact
        double maxCarCarImpactSpeed = 0.0;
        int damagePlayerTotal = 0, damageAmbientTotal = 0;
        double minSpeedAfterPedHit = 1e9, speedBeforePedHit = 0.0;
        uint64_t knockRecoveries = 0;
    } vcStats;
    sr3airuntime::NotorietyRoadblockScheduler roadblockSchedule;
    bool roadblockDebugForce = false;  // 'R' toggles this (debug trigger); notoriety arms independently
    uint64_t roadblockSchedAttempts = 0;
    uint64_t roadblockSchedPlaced = 0;
    uint64_t roadblockDebugPlaced = 0;
    std::unique_ptr<sr3airuntime::NpcAiRuntime> pedAi;
    std::unordered_set<uint64_t> pedAiIds;  // ped ids currently registered with pedAi
    std::unordered_set<uint64_t> pedAiCowering;  // peds the AI has put into Cower this frame (skip the idle walk)
    bool pedAiPlayerFiring = false;  // the player fired last frame (a civilian's gunfire threat input)
    uint64_t pedAiFleeCount = 0;
    uint64_t pedAiCowerCount = 0;
    std::unordered_map<uint64_t, sr3airuntime::PedReactionMode> nearFieldPedReaction;
    std::unordered_map<uint64_t, sr3airuntime::Vec3> nearFieldPedFleeDestination;
    std::unordered_map<uint64_t, sr3airuntime::Vec3> nearFieldPedWanderDestination;
    std::unordered_map<uint64_t, sr3airuntime::SidewalkSegment> nearFieldPedSidewalkSegGeo;
    bool prevFireDown = false;

    // --- setupStreamingGround (original lines 2832-3211) ---
    std::map<size_t, std::unique_ptr<CityLoadedCell>> loaded;
    std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>> groundTiles;
    size_t totalLoads = 0;
    size_t totalEvicts = 0;
    std::optional<ClmeshTriangleCollisionWorld> collisionWorld_;
    std::optional<RealCameraCollision> realCameraCollision_;
    std::optional<RealPropLineOfSight> npcLosWorld_;  // AI-3: NPC line of sight over the same loaded prop collision
    std::function<void(size_t, int)> loadCell;
    std::vector<size_t> lastWanted;
    bool firstReconcile = true;
    std::function<void(int, float, float)> reconcile;
    float finalGroundY = 0.0f;
    std::optional<sr3player::CharacterProxyDriver> collisionDriver_;
    std::map<std::pair<int, int>, sr3zone::HeightmapRecord> heightmapTiles;
    std::map<std::pair<int, int>, bool> heightmapTileFailed;  // avoids retrying a tile every single frame
    std::function<float(double, double, float)> sampleRealGroundHeight;
    // Walkable ground (TEAM B task G1, spec-zone-data-format.md Sec13.5.1): (x, z, startY, holdY) -> ground Y + which
    // source supplied it. The ray starts at startY + 0.05 (closest hit = highest surface at/below); `holdY` is returned
    // unchanged when nothing resolves ("on a miss, leave Y unchanged"). 0x2244 is never consulted.
    enum class GroundSource { Mesh, MeshRetry, NodeFallback, HeldMiss, HeldNoTile };
    struct GroundSampleResult {
        float y = 0.0f;
        GroundSource source = GroundSource::HeldNoTile;
        bool real() const { return source == GroundSource::Mesh || source == GroundSource::MeshRetry; }
    };
    std::function<GroundSampleResult(double, double, double, float)> sampleGround;
    // Last real ground target per follower (a "hold" keeps easing toward it instead of freezing mid-ease).
    float playerGroundTarget = 0.0f;
    float vehicleGroundTarget = 0.0f;
    uint64_t groundSrcCount[5] = {0, 0, 0, 0, 0};  // per GroundSource, player queries only (diagnostic)
    std::optional<sr3player::GroundFollower> playerGroundFollower_;
    std::optional<sr3player::GroundFollower> vehicleGroundFollower_;
    float vehicleGroundY{};  // updated every frame below once the vehicle is loaded; render code
    std::optional<sr3xtbl::Document> cameraFreeDoc_;
    std::optional<sr3player::CameraFreeSeedReport> camSeedReport_;
    HWND hwnd{};
    bool cursorHidden = false;
    float kMouseRadPerCount{};

    // --- setupVehicle (original lines 3213-3297) ---
    std::optional<PlaytestVehicleRig> vehicleRig_;
    sr3viewer_craft::CraftSessionState craft;  // AW-15: aircraft / boat rig + freefall (viewer_craft_wiring.h)
    // Ambient model variety: per-category model choice + caches; per-actor assigned model (null = placeholder).
    std::optional<AmbientModelRegistry> ambientModels_;
    std::unordered_map<uint64_t, AmbientVehicleModel*> nearFieldVehicleModel;
    std::unordered_map<uint64_t, AmbientPedModel*> nearFieldPedModel;
    struct PedAnimState { double cycles = 0.0, lastX = 0.0, lastZ = 0.0; bool valid = false; };
    std::unordered_map<uint64_t, PedAnimState> nearFieldPedAnim;  // walk phase (cycles) driven by distance moved
    bool inVehicle = false;
    sr3player::VehicleChaseCamera vehicleChaseCam;       // Sec4.15 driver chase camera
    sr3player::VehicleCameraStats vehicleChaseCamStats;  // headless "vehicle-camera-trace:" accumulator
    bool vehicleChaseCamWasDriving = false;
    bool prevEDown = false;
    bool prevTDown = false;
    bool prevRadioPrevDown = false;
    bool prevRadioNextDown = false;  // radio station keys ',' '.'
    bool vehiclePromptShown = false;
    int kScriptVehicleExitFrame{};

    // --- enter-ambient-cars (free-roam task) ---
    // The single PlaytestVehicleRig is the "active car": the one the player drives, and (when not driving) the one
    // parked at its last pose. Entering any other car swaps that car's pose/velocity INTO the rig and files the
    // rig's previous car in parkedVehicles (same model, drawn at its stored pose). Ids: rig = kAmbientRigCarId,
    // ambient cars keep their near-field id, parked cars use kParkedIdBase + n.
    struct ParkedVehicle {
        uint64_t id = 0;
        double x = 0.0, z = 0.0, heading = 0.0;
        float y = 0.0f;
        bool isGenki = false;  // the original car (informational only since EC-6: it gets no despawn exemption)
        AmbientVehicleModel* model = nullptr;  // own mesh (ambient-ped-anim task); null = the player demo car's rig
        // EC-6 (spec-ai-runtime.md Sec20.6.14(d)): the abandoned-schedule entry timer (started at first collection)
        // and the end of the 1000 ms fade of an in-view pick (deleted when it passes).
        int64_t entryStartMs = -1;
        int64_t fadeEndMs = -1;
    };
    // EC-6: the abandoned-car schedule (shipped rows). The car the rig holds is the local player's lastVehicle
    // (Sec20.6.14(b)): protected by gate 7 (10 km), so only the cars in parkedVehicles are ever candidates.
    sr3airuntime::AbandonedVehicleSchedule abandonedSchedule;
    int abandonedPicksOut = 0, abandonedPicksIn = 0;
    // The model the active (rig) car is drawn with: the entered ambient car's own mesh (nearFieldVehicleModel[id]),
    // null while the original parked car is the active one. Physics/handling stay the single rig's row (CHOSEN).
    AmbientVehicleModel* activeCarModel = nullptr;
    std::vector<ParkedVehicle> parkedVehicles;
    uint64_t nextParkedId = 0;
    bool rigCarIsGenki = true;
    int scriptEnterAmbientAt = -1;  // --script-enter-ambient-at=FRAME (--no-input): warp beside nearest ambient car, E, drive, E
    int scriptWarpToPedAt = -1;     // --script-warp-to-ped-at=FRAME (evidence helper: player warps 3 m behind the nearest ambient ped)
    int scriptEnterAmbientDriveFrames = 90;  // --script-enter-ambient-drive-frames=N
    int scriptEnterAmbientCount = 1;         // --script-enter-ambient-count=N cycles (each carjacks the nearest ambient car)
    bool scriptEnterAmbientReenter = false;  // --script-enter-ambient-reenter: re-enter the car just left, then exit
    int scriptVehicleEnterFrame = 10;  // --script-vehicle-enter-frame=N (default 10, the original constant)
    int scriptEnterAmbientReenterDriveFrames = 30;  // --script-enter-ambient-reenter-drive-frames=N: frames before the re-entry exit (default 30)
    sr3player::AmbientEntryStats ambientEntryStats;
    bool ambientDriving = false;  // the car being driven is not the original parked car
    double ambientPrevX = 0.0, ambientPrevZ = 0.0;

    // --- setupTraffic (original lines 3299-3484) ---
    std::vector<sr3airuntime::NearFieldVehicle> nearFieldLive;
    uint64_t nearFieldNextId = 1;
    std::optional<sr3airuntime::NearFieldVehicleDriver> nearFieldDriver_;
    std::optional<sr3airuntime::AmbientToggleState> nearFieldToggles_;  // every category enabled by default, unchanged (reused, not redeclared)
    std::optional<sr3airuntime::DeterministicRng> nearFieldRng_;
    std::function<size_t(size_t)> nearFieldPickUniform;
    sr3airuntime::Vec3 fpCamCur{0, 0, 0};
    sr3airuntime::Vec3 fpCamPrev{0, 0, 0};
    sr3airuntime::Vec3 fpCamFwd{0, 0, 1};
    int fpCamSamples = 0;
    sr3airuntime::Vec3 freeroamPrevRefPos{0, 0, 0};  // free-roam population: last frame camera position (camera velocity)
    size_t freeroamVehDespawned = 0;
    size_t freeroamVehPasses = 0;
    size_t freeroamPedDespawned = 0;
    bool freeroamHavePrev = false;
    std::vector<uint8_t> trafficLaneGraphBuf;
    std::unique_ptr<sr3airuntime::TrafficLaneGraph> trafficLaneGraphOwner;
    sr3airuntime::TrafficLaneGraph* trafficLaneGraph_p{};
    std::unordered_map<uint64_t, sr3airuntime::LaneFollowState> nearFieldVehicleLaneState;
    std::unordered_map<uint64_t, double> nearFieldVehicleHeading;
    // T3 actor placement: per-ambient-actor cached ground Y (re-sampled only after moving) and, for peds, facing.
    struct ActorPlacement {
        double sampX = 0.0, sampZ = 0.0, lastX = 0.0, lastZ = 0.0;
        float y = 0.0f;
        double heading = 0.0;
        bool valid = false;
        float sampledMinusNode = 0.0f;  // last ground sample minus the actor's own graph-node Y
    };
    std::unordered_map<uint64_t, ActorPlacement> nearFieldVehiclePlace;
    std::unordered_map<uint64_t, ActorPlacement> nearFieldPedPlace;
    // ---- police response to notoriety (feature/police-response; sr3airuntime/police_response.h) ----
    struct PoliceUnit {
        uint64_t id = 0;
        bool car = false;
        sr3airuntime::Vec3 pos;
        double heading = 0.0, speed = 0.0;
        bool standDown = false;
        int64_t standDownAtMs = 0, spawnedMs = 0;
        bool handedOff = false, crewReleased = false;
        int64_t handoffAtMs = 0;
        bool dead = false;
        int64_t deadAtMs = 0;
        double hp = 100.0;     // hit points taken from homie fire (Sec24.9); the player's own fire kills via the engine character
        ActorPlacement place;  // render-side cached ground Y / facing
        AmbientVehicleModel* vmodel = nullptr;
        AmbientPedModel* pmodel = nullptr;
        bool modelResolved = false;
    };
    struct PoliceState {
        std::vector<PoliceUnit> units;
        sr3airuntime::PoliceDispatcher dispatcher;
        std::unique_ptr<sr3airuntime::NpcAiRuntime> ai;  // on-foot cops (Combatant kind), the Sec21.1 loop
        uint64_t nextId = 1;
        uint64_t rng = 0x9E3779B97F4A7C15ull;
        // Resolved police models (registry-owned, so per session; were function-level statics = dangling after a Load).
        bool modelsTried = false;
        AmbientVehicleModel* carModel = nullptr;
        AmbientPedModel* pedModel = nullptr;
        bool haveLastPlayer = false;
        sr3airuntime::Vec3 lastPlayerPos;
        sr3airuntime::Vec3 playerVel;
        // trace counters
        uint64_t spawnedFoot = 0, spawnedCar = 0, spawnFailCar = 0, crewReleased = 0, handoffs = 0;
        uint64_t shots = 0, hits = 0, disengages = 0, removed = 0, killed = 0, seenTicks = 0;
        double damageToPlayer = 0.0;
        int maxFoot = 0, maxCar = 0;
        std::array<uint64_t, 6> spawnedByLevel{};
        double minCarGap = 1e30;
        bool wasEngaged = false;
        int lastLevel = 0;
    } police;
    // ---- homies and followers (feature/homies; sr3airuntime/homies.h, spec-ai-runtime.md Sec24) ----
    struct HomieRowState {
        sr3tables_trafficai::Homie row;
        sr3airuntime::homies::Record rec;
    };
    struct HomieMember {
        uint64_t id = 0;
        int rowIdx = -1;
        std::string charName;      // engine character (party membership key)
        std::string humanRow;      // homies.xtbl Humans/Character
        sr3airuntime::Vec3 pos;
        double heading = 0.0;
        enum class Phase { InCar, OnFoot, Boarding, Seated, Stand } phase = Phase::InCar;
        int seat = -1;
        bool joined = false;       // in the player's party (Sec24.5 step 5: at spawn, still driving up)
        bool canLeave = false;     // can_leave_behind
        double hp = 100.0, maxHp = 100.0;
        bool downed = false;
        int64_t downedUntilMs = 0, reviveHoldMs = 0;
        sr3airuntime::homies::FriendlyFireMeter ff;
        sr3airuntime::homies::AbandonTracker ab;
        int64_t waitUntilMs = 0;
        int gait = 0;
        int64_t standUntilMs = 0;  // a non-joining row (turn to ped after driveup) is removed after this
        uint64_t aiHandle = 0;
        AmbientPedModel* pmodel = nullptr;
        ActorPlacement place;
        bool dead = false;
    };
    struct HomieState {
        bool loaded = false, loadFailed = false;
        std::vector<HomieRowState> rows;
        std::vector<HomieMember> members;
        // the one call in flight (state word 0x022ad6cc): 0 none, 1 ringing, 2 connected, 3 driving up, 4 arrived
        int callPhase = 0;
        int callRow = -1;
        int64_t callPhaseStartMs = 0;
        // Phone close (Sec16a.2 FINAL): the phone leaves state 5 (and accepts a new dial) 3.033 s after the summon step.
        int64_t phoneCloseAtMs = -1;
        // lower-cased character.xtbl row -> Spawn_Info_Rank Hit_Points (built once from character.xtbl + spawn_info_ranks.xtbl)
        bool rankHpLoaded = false;
        std::unordered_map<std::string, double> rankHpByCharacter;
        std::vector<uint64_t> callMembers;
        sr3airuntime::Vec3 carPos;
        double carHeading = 0.0, carSpeed = 0.0, carTopSpeed = 40.0;
        bool carStarted = false;  // the drive-up car has exceeded the 1.0 m/s arrival speed once (arrival is speed < 1.0)
        AmbientVehicleModel* carModel = nullptr;
        std::string carName;
        struct ParkedCar { sr3airuntime::Vec3 pos; double heading = 0.0; AmbientVehicleModel* model = nullptr; int64_t sinceMs = 0; };
        std::vector<ParkedCar> parkedCars;
        std::unique_ptr<sr3airuntime::NpcAiRuntime> ai;  // the followers' combat loop: the "player" it targets is the enemy
        uint64_t nextId = 1;
        uint64_t rng = 0xC0FFEE1234567ull;
        int clockDays = 0;
        double lastTod = -1.0;
        bool haveLastPlayer = false, lastInVehicle = false;
        sr3airuntime::Vec3 lastPlayerPos, playerVel;
        uint64_t enemyHandle = 0;
        int64_t nextTargetCopyMs = 0;
        size_t lastAbandonCount = 0;
        std::vector<bool> firedCall, firedDismissOne, firedDismissAll, firedFriendly, firedWanted;
        bool firedInjure = false;
        // counters
        uint64_t calls = 0, busy = 0, summons = 0, spawnFails = 0, arrivals = 0, dropped = 0, dismissed = 0;
        uint64_t shots = 0, hits = 0, kills = 0, boarded = 0, exited = 0, teleports = 0, abandoned = 0;
        uint64_t downedCount = 0, revived = 0, deaths = 0, angryLeft = 0, squadWaits = 0;
        double billsDollars = 0.0;
        int maxFollowers = 0;
    } homies;
    uint64_t actorGroundSamples = 0;
    std::unordered_map<uint64_t, double> nearFieldVehicleSpeed;
    std::unordered_map<uint64_t, sr3airuntime::Vec3> nearFieldVehicleStartPosForTrace;
    uint64_t overtakeGateFireCountForTrace = 0;
    std::unordered_map<uint64_t, std::pair<uint64_t, double>> nearFieldVehicleBrakeMemory;
    std::optional<sr3airuntime::TrafficSignalController> signalController_;
    std::unordered_map<uint64_t, sr3airuntime::StopLineMemory> nearFieldVehicleStopLine;
    std::unordered_map<uint64_t, std::pair<int32_t, int>> nearFieldVehicleSignalWait;  // seg, kind 1 light / 2 stop line
    uint64_t signalSwitchCount = 0;
    uint64_t signalAmberCount = 0;
    uint64_t signalCarStops = 0;
    uint64_t signalCarReleases = 0;
    uint64_t stopLineCarStops = 0;
    uint64_t stopLineCarReleases = 0;
    uint64_t signalCarFramesOnControlled = 0;
    uint64_t signalCarFramesOnRedAmber = 0;
    // Traffic review counters (RG-12/RF-3/yield): lane-follower re-queries, obstacle-probe runs / reuses, yield brakes.
    uint64_t laneRequeryCount = 0;
    uint64_t obstacleProbeRuns = 0;
    uint64_t obstacleProbeReuses = 0;
    uint64_t yieldBrakeFrames = 0;
    // Per-car a_b diagnostics (feature/ambient-per-car-braking): car-frames on the lane-follow path, how many used a real
    // vehicle-row AI_Max_Braking_Decel, distinct car ids that ever used a row / ever used the fallback, min/max a_b seen.
    uint64_t aBCarFrames = 0;
    uint64_t aBRowFrames = 0;
    std::unordered_set<uint64_t> aBRowCars, aBFallbackCars;
    double aBMin = 1e300, aBMax = -1e300;
    uint64_t yieldStops = 0;
    uint64_t yieldRuns = 0;
    uint64_t yieldConflictsFound = 0;
    // RF-3: per-car obstacle-probe re-arm timer + the last probe result (reused between runs, Sec20.6.7(a)); junction
    // yield timer (150-300 ms) + its last decision (Sec20.6.7(c)).
    struct ProbeCache {
        double timerMs = 0.0;
        bool hasLead = false;
        sr3airuntime::FollowingResult result;
    };
    struct YieldCache {
        double timerMs = 0.0;
        bool yielding = false;
        double dist = 0.0;      // distance to the conflict start measured at the first yielding run
        double travelled = 0.0;  // distance driven since
        bool stopped = false;
        uint32_t runs = 0;
    };
    std::unordered_map<uint64_t, ProbeCache> nearFieldVehicleProbe;
    std::unordered_map<uint64_t, YieldCache> nearFieldVehicleYield;
    std::vector<uint8_t> signalControlledLane;
    std::unordered_map<uint64_t, sr3airuntime::PedCrossingState> nearFieldPedCrossing;
    uint64_t pedCrossWaits = 0;
    uint64_t pedCrossCrosses = 0;
    uint64_t pedCrossAfterWait = 0;
    uint64_t pedCrossUnsignalled = 0;
    std::unordered_map<uint64_t, sr3airuntime::PedNavigator> nearFieldPedNav;
    std::unordered_map<uint64_t, double> nearFieldPedWalkSpeed;  // per-ped walk-variant speed, picked once (Sec20.6.9(a))
    uint64_t pedNavSearches = 0;
    uint64_t pedNavSearchFails = 0;
    uint64_t pedNavArrivals = 0;
    uint64_t pedNavEdgesWalked = 0;
    uint64_t pedNavEvent8 = 0;
    std::unordered_map<uint64_t, int64_t> pedNavEvent8Due;  // ped -> due time of the delayed event 8
    std::vector<sr3airuntime::NearFieldPed> nearFieldPedsLive;
    uint64_t nearFieldPedNextId = 1;
    std::optional<sr3airuntime::NearFieldPedDriver> nearFieldPedDriver_;
    std::optional<sr3airuntime::DeterministicRng> nearFieldPedRng_;  // own seed, distinct from nearFieldRng above - independent RNG stream, same "one driver, one rng" convention every other sr3airuntime driver in this file uses
    std::function<size_t(size_t)> nearFieldPedPickUniform;
    std::optional<sr3airuntime::NearFieldPedSpawnGates> nearFieldPedGates_;
    size_t kNearFieldPedLocalCap{};

    // --- setupRoadblockOccupant (original lines 3486-3598) ---
    std::vector<sr3airuntime::RoadblockSlot> roadblockSlots;
    std::optional<sr3airuntime::RoadblockSpawnManager> roadblockManager_;
    std::optional<sr3airuntime::RoadblockPopulationDriver> roadblockDriver_;  // armed=false; 'R' toggles it live (see main loop)
    bool prevRDown = false;
    std::vector<std::optional<sr3airuntime::RoadblockDispatchResult>> roadblockSlotVisual;
    bool vehicleHideOccupant = false;

    // --- setupLoopState (original lines 3600-3766) ---
    LARGE_INTEGER freq{};
    LARGE_INTEGER prevTime{};
    int frames = 0;
    int64_t nowMsAccum = 0;
    bool missionTimedOutDump = false;  // fix/auto-objective-driving-2: --mission-timeout hit -> still dump live script threads' wait sites at session end
    bool autoObjectiveFired = false;  // --auto-objective (get_dist stall): fires at most once per run
    std::unordered_set<std::string> autoObjectiveVisitedTriggers;  // --auto-objective (trigger wait): triggers already teleported into
    bool autoSkipCutscenesApplied = false;
    AutoRoute autoRoute;
    int autoRouteHoldFramesLeft = 0;
    int autoRouteHoldAge = 0;
    sr3luahost::WorldPosition autoRouteHoldPos{};
    int autoVehicleWaitFrames = 0;
    int autoEnterHookWaitFrames = 0;
    std::unordered_map<std::string, int> autoKillPollFrames;
    std::unordered_set<std::string> autoKilledNames;
    int autoKillIdleFrames = 0;
    std::optional<sr3luahost::autocombat::Config> autoCombatCfg_;
    std::optional<sr3luahost::autocombat::State> autoCombatState_;
    std::unordered_set<std::string> autoCombatSeen;
    std::unordered_set<std::string> autoCombatCounted;
    bool autoCombatWeaponReady = false;
    std::optional<AutoCombatDamageSink> autoCombatSink_;
    int autoKillIntervalFrames{};
    std::unordered_map<std::string, int> autoCallAgeFrames;
    std::unordered_map<std::string, int> autoShieldPollFrames;
    std::unordered_map<std::string, int> autoRcPollFrames;
    std::unordered_set<std::string> autoShieldDone;
    std::unordered_set<std::string> autoRcDone;
    int autoHudScreenWaitFrames = 0;
    bool autoHudScreenPopped = false;
    int autoCallTalkFrames = 150;
    std::function<std::string(std::string)> lowerStr;
    std::unordered_set<std::string> autoObjectiveNpcMoved;  // "npc|target" get_dist pairs already placed
    bool autoSeatMissionVehicleFired = false;  // --auto-seat-mission-vehicle: fires at most once per run (frame 0 of mission drive)
    double todFraction = 0.5;
    std::function<void()> tryLoadRealPauseDoc;

    // Pushes the scheduler's current state into both consumers: ped density (current stage's PedDensity,
    // spec-ai-runtime Sec20.6.0(d)) and the lighting corner names/weight (see ResolveLightingStage()).
    void weatherRefreshConsumers() {
        if (!weatherSched) return;
        const auto st = weatherSched->Current();
        freeroamWeatherPedDensity = weatherSched->PedDensity(st.currentStage);
        weatherLightA = sr3render::ResolveLightingStage(weatherSched->Stages(), st.stageA, todSegments);
        weatherLightB = sr3render::ResolveLightingStage(weatherSched->Stages(), st.stageB, todSegments);
        weatherLightBlend = st.blend;
    }

    // One sim step: advances the scheduler (dt sim seconds x weatherTimeScale), traces stage changes
    // ("weather-trace:" with the TOD clock and the real/sim time) and refreshes the consumers.
    void weatherTick(double dt, int frame) {
        if (!weatherSched) return;
        const double step = dt * weatherTimeScale;
        weatherSimSeconds += step;
        int hh = 0, mm = 0;
        sr3render::TodFractionToHourMinute(todFraction, hh, mm);
        weatherSched->Tick(step, [&](int from, int to) {
            const auto cs = weatherSched->Current();
            std::printf("weather-trace: stage-change frame=%d weather_t=%.1fs tod=%02d:%02d %s -> %s pedDensity=%.2f "
                        "duration=%.1fs\n",
                        frame, weatherSimSeconds, hh, mm, weatherSched->StageName(from).c_str(),
                        weatherSched->StageName(to).c_str(), static_cast<double>(weatherSched->PedDensity(to)),
                        cs.duration);
        });
        weatherRefreshConsumers();
        if (weatherTraceFrames > 0 && frame % weatherTraceFrames == 0) {
            const auto cs = weatherSched->Current();
            std::printf("weather-trace: state frame=%d weather_t=%.1fs tod=%02d:%02d stage=%s other=%s blend=%.3f "
                        "elapsed=%.1f/%.1fs light=%s/%s pedDensity=%.2f%s\n",
                        frame, weatherSimSeconds, hh, mm, weatherSched->StageName(cs.stageA).c_str(),
                        weatherSched->StageName(cs.stageB).c_str(), static_cast<double>(cs.blend), cs.elapsed,
                        cs.duration, weatherLightA.c_str(), weatherLightB.c_str(),
                        static_cast<double>(freeroamWeatherPedDensity), cs.pinned ? " (pinned)" : "");
        }
    }
};

// ---- per-frame locals shared between the frame-loop steps (a fresh one per loop iteration) ----
struct PlaytestFrame {

    // --- stepFrameBegin (original lines 3769-3781) ---
    double dt{};

    // --- stepInput (original lines 3783-3955) ---
    sr3physics::DriverInput vehInputLive;
    bool vehEDownNow = false;
    bool tDownNow = false;
    bool radioPrevDownNow = false;
    bool radioNextDownNow = false;  // CHOSEN D-pad stand-ins ',' '.'
    bool roadblockKeyDownNow = false;
    bool fireKeyDownNow = false;
    bool reloadKeyDownNow = false;     // gap #17: R (CHOSEN binding)
    uint32_t weaponSelKeysDownNow = 0;  // gap #17: bits 0-8 = keys 1-9, bit 9 = Q (prev), bit 10 = Tab (next)

    // --- stepWorld (original lines 4048-4119) ---
    float focusX{};
    float focusZ{};

    // --- stepTrafficSignals (original lines 4121-4153) ---
    sr3airuntime::Vec3 trafficReferencePos;
    double trafficReferenceHeadingRad{};
    sr3airuntime::Vec3 trafficReferenceForward;

    // --- stepPlayerView (original lines 5087-5600) ---
    float viewProjection[16];
    double headingNow = 0.0;
    float charWorld[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // overwritten below in BOTH branches now
    sr3animpose::PoseComposeResult poseResult;
    float diagEyeX = 0.0f;
    float diagEyeY = 0.0f;
    float diagEyeZ = 0.0f;
};

// ---- helpers formerly in viewer_playtest.cpp's anonymous namespace (defined in viewer_playtest.cpp) ----
bool SampleGroundMeshHeight(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                            double z, double startY, double& outY);
double SampleGroundMeshHoleFill(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                                double z, double startY, double& outY);
bool SampleGroundMeshTopHeight(const std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>>& tiles, double x,
                               double z, double& outY);
bool loadCityHeightmapTile(int col, int row, std::vector<std::unique_ptr<CityArchive>>& archives,
                           sr3zone::HeightmapRecord& outRecord, std::string& why);
std::string narrowLocText(const std::u16string& s);

// ---- runPlaytest() split: setup sections (return -1 to continue, else the exit code runPlaytest() returns) ----
int setupArgs(PlaytestSession& sess, int argc, char** argv);
int setupBrad(PlaytestSession& sess);
int setupTables(PlaytestSession& sess);
int setupAnimRuntime(PlaytestSession& sess);
int setupPoseAndInput(PlaytestSession& sess);
int setupCityArchives(PlaytestSession& sess);
int setupWindowDevice(PlaytestSession& sess);
int setupAudio(PlaytestSession& sess);
int setupRenderAssets(PlaytestSession& sess);
int setupHudAndMission(PlaytestSession& sess);
int setupNpcAiState(PlaytestSession& sess);
int setupStreamingGround(PlaytestSession& sess);
int setupVehicle(PlaytestSession& sess);
int setupTraffic(PlaytestSession& sess);
int setupRoadblockOccupant(PlaytestSession& sess);
int setupLoopState(PlaytestSession& sess);

// ---- frame-loop steps, called in this order every frame (bool steps return true to leave the loop) ----
bool stepFrameBegin(PlaytestSession& sess, PlaytestFrame& frm);
bool stepInput(PlaytestSession& sess, PlaytestFrame& frm);
void stepPlayerVitals(PlaytestSession& sess, PlaytestFrame& frm);
void soakStep(PlaytestSession& sess, PlaytestFrame& frm);  // viewer_playtest_soak.cpp (end of stepInput)
void stepVehicleControls(PlaytestSession& sess, PlaytestFrame& frm);
void stepWorld(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficSignals(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficVehicles(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficPeds(PlaytestSession& sess, PlaytestFrame& frm);
void stepVehicleCollisions(PlaytestSession& sess, PlaytestFrame& frm);  // gap #8: player car vs peds / cars
void printVehicleCollisionTrace(const PlaytestSession& sess);
void stepTrafficRoadblock(PlaytestSession& sess, PlaytestFrame& frm);
void stepNotoriety(PlaytestSession& sess, PlaytestFrame& frm);
void stepPolice(PlaytestSession& sess, PlaytestFrame& frm);  // police response to notoriety: spawn / chase / shoot / stand down
void printPoliceTrace(const PlaytestSession& sess);
void stepHomies(PlaytestSession& sess, PlaytestFrame& frm);  // homies and followers (spec-ai-runtime.md Sec24)
void printHomieTrace(const PlaytestSession& sess);
void notorietyReportCrime(PlaytestSession& sess, const sr3airuntime::CrimeActivity& activity, const char* what);
void stepPlayerView(PlaytestSession& sess, PlaytestFrame& frm);
void stepMissionBridge(PlaytestSession& sess, PlaytestFrame& frm);
void stepMissionDrive(PlaytestSession& sess, PlaytestFrame& frm);
void stepGps(PlaytestSession& sess, PlaytestFrame& frm);  // GPS waypoint planner tick (Sec23), "gps-trace:"

// AW-15 (viewer_playtest_craft.cpp): aircraft / boat rig and freefall wiring.
const sr3viewer_craft::CraftTypeRow* craftRowForActiveCar(const PlaytestSession& sess);
float craftParkedRenderY(const PlaytestSession& sess, float groundY);  // mesh-origin Y of the active parked car on `groundY`
void craftSpawnStep(PlaytestSession& sess);
void craftPlayerLiftStep(PlaytestSession& sess);
void craftOnEnter(PlaytestSession& sess);
void craftOnExitBefore(PlaytestSession& sess);
void craftOnExitAfter(PlaytestSession& sess);
void craftStepWorld(PlaytestSession& sess, PlaytestFrame& frm);
void craftWorldMatrix(PlaytestSession& sess, float fallbackY, float out[16]);
void craftParkedWorldMatrix(const PlaytestSession& sess, const PlaytestSession::ParkedVehicle& pk, float out[16]);
void freefallPreStep(PlaytestSession& sess, PlaytestFrame& frm, double groundRawY, bool groundReal);
void freefallPostStep(PlaytestSession& sess);
void printCraftSummary(const PlaytestSession& sess);
void printGpsTrace(const PlaytestSession& sess);
bool stepRender(PlaytestSession& sess, PlaytestFrame& frm);

// ---- end-of-run trace printing / cleanup ----
void reportPlaytest(PlaytestSession& sess);
// Save/load through sr3saveflow (viewer_playtest_saveflow.cpp).
void applyLoadedSave(PlaytestSession& sess);
void writeSaveAtExit(PlaytestSession& sess);
// Writes the free-roam save to `path` (what --save-at-exit does at exit). Returns true when the file was written.
// With sess.writePositionSidecar, also writes `<path>.sr3pos` (CHOSEN recomp extension, see saveflow.cpp).
bool writeSaveTo(PlaytestSession& sess, const std::string& path);
// The whole playtest (setup phases, frame loop, report) over an already-constructed session whose
// sess.hooks may be set. runPlaytest() is this with no hooks. Returns the process exit code.
int runPlaytestSession(PlaytestSession& sess, int argc, char** argv);
// Activities through sr3activity (viewer_playtest_activity.cpp).
void setupActivity(PlaytestSession& sess);
void stepActivity(PlaytestSession& sess, PlaytestFrame& frm);
// The time-of-day sky gradient (viewer_playtest_sky.cpp), drawn after the clear.
// A HUD bar fill with the real ui_hud_meter_fill sheet art (viewer_playtest_hudart.cpp), flat if absent.
void drawHudBarFill(PlaytestSession& sess, double x0, double y0, double x1, double y1, const uint8_t rgba[4]);
// The police notoriety icons (spec-hud-runtime.md H.2 row "Police notoriety"): `lit` icons of
// ui_hud_not_police around the minimap centre (cx, cy), ui = the HUD scale.
void drawHudNotorietyIcons(PlaytestSession& sess, double cx, double cy, double ui, int lit);
// A vint bitmap_circle pie of a HUD sheet image (spec-vint-render.md 13.2), and the respect meter ring
// (spec-hud-runtime.md H.2) filled to pct (0..1).
void drawHudBitmapCircle(PlaytestSession& sess, const std::string& image, double cx, double cy, double halfW,
                         double halfH, double startAngle, double endAngle, int numWedges, const uint8_t rgba[4]);
void drawHudRespectMeter(PlaytestSession& sess, double ui, double pct);
void drawSkyGradient(PlaytestSession& sess, const float viewProjection[16], double todFraction);
// One exposure for the scene's ambient + sun (viewer_playtest_sky.cpp), replacing the per-colour normalisation.
void applySceneExposure(PlaytestSession& sess, double todFraction, float ambient[3], float sun[3]);
void setupCollect(PlaytestSession& sess);
void stepCollect(PlaytestSession& sess, PlaytestFrame& frm);
void drawActivityHud(PlaytestSession& sess, sr3luahost::EngineState& es, const HudDemoFont& font,
                     VintQuadPipeline& pipe, ID3D11DeviceContext* ctx);

} // namespace sr3viewer
