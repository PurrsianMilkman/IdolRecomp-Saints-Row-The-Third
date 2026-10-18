#pragma once

// Shared state for the `sr3_viewer playtest` command (runPlaytest()).  runPlaytest() used to be one
// ~6,900-line function; its locals that are shared between the setup sections, the frame loop and the
// end-of-run report now live in PlaytestSession (setup-time / whole-run state, members in the order the
// old locals were declared) and PlaytestFrame (per-frame locals shared between the frame-loop steps).
// Members that needed a non-trivial constructor/initialiser are std::optional<T> named `<name>_`
// (emplaced at the point the old local was declared); each setup/step function binds `auto& name = ...`
// references at its top so the moved code reads exactly as it did inside runPlaytest().

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
#include "sr3zone/zone_geometry.h"
#include "sr3zone/zone_header.h"
#include "sr3zone/zone_objects.h"
#include "sr3zone/traffic_graph.h"
#include "sr3airuntime/traffic_following.h"
#include "sr3airuntime/traffic_signals.h"
#include "sr3airuntime/traffic_yield.h"
#include "sr3airuntime/ped_crossing.h"
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
// RF-3 (Sec20.6.3(b)): a_b = vehicle +0xc58 = AI_Max_Braking_Decel x condition scale. CHOSEN stand-in: every ambient car
// here is drawn as one model with no vehicle-info row, so AI_Max_Braking_Decel = 12.06 (the car_4dr_standard03 value of
// the Sec20.6.12 test vector), condition level 0 (no vehicle damage on ambient cars). Replaces the fixed 8.0.
constexpr double kNearFieldVehicleAiMaxBrakingDecel = 12.06;
const double kNearFieldVehicleBrakeDecel = sr3airuntime::ConditionScaledBrakingDecel(kNearFieldVehicleAiMaxBrakingDecel, 0);
// RF-2 (Sec20.6.13(f)): CHOSEN stand-in for the per-vehicle mode byte (+0xc68) and obey bit (+0xc6c bit 0): every
// near-field car is an ambient car in mode 8 with the bit set (no chase/flee/rejoin cars exist in this population).
constexpr int kNearFieldVehicleMode = sr3airuntime::kVehicleModeAmbient;
constexpr bool kNearFieldVehicleObeysLights = true;
constexpr double kNearFieldVehicleHalfLength = 2.2;
constexpr double kNearFieldVehicleProbeHalfWidth = 2.0;
constexpr double kSignalResidentRadius = 400.0;
constexpr double kSignalLogRadius = 150.0;
constexpr uint64_t kSignalMemoryId = ~0ull - 2;
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
    bool pressed(sr3player::MoveAction, int*) override { return false; }
    void consume(sr3player::MoveAction) override {}
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
    ~RoadGraphGlobalReset() { g_playtestRoadGraph = nullptr; }
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
struct PlaytestSession {

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
    int debugNotoriety = -1;
    int debugDamagePlayer = -1;  // --debug-damage-player=N: DEBUG damage of N applied to the player at frame 30 (T1)
    bool scriptRoadblock = false;
    bool scriptFire = false;
    // T2 jump/crouch: --script-jump-at=FRAME (repeatable), --script-crouch-frames=A-B (inclusive), --jump-height=M (CHOSEN override)
    std::vector<int> scriptJumpAt;
    int scriptCrouchA = -1, scriptCrouchB = -1;
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
    std::string kWeatherStageForPlaytest = "Light Rain";  // LIGHTING stage only (full weather_time_of_day coverage)
    // Ped-density weather stage (Sec20.6.0(d)). CHOSEN: the spec gives no fixed game-start stage (the stage state
    // machine picks by Chance); "Clear Skies" (highest-Chance stage, PedDensity 1.0) is used. Override: --weather=<stage>.
    std::string kWeatherStageForPedDensity = "Clear Skies";
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
    std::unique_ptr<sr3weapons::WeaponInstance> bradWeapon;

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
    std::vector<uint8_t> loadedSaveImage;
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
    std::string vitalsTrace;  // accumulated player-vitals-trace event lines (printed at exit)
    int debugNotorietyPoints = -1;  // --debug-notoriety-points=N: DEBUG crime of N police points at frame 1
    uint64_t crimeReports = 0;
    uint64_t notorietyLevelChanges = 0;
    std::unordered_set<uint64_t> vehicleHitPedIds;  // peds already reported as hit by the player's vehicle
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
    std::function<float(double, double, float)> sampleGroundHeight;
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
    // Ambient model variety: per-category model choice + caches; per-actor assigned model (null = placeholder).
    std::optional<AmbientModelRegistry> ambientModels_;
    std::unordered_map<uint64_t, AmbientVehicleModel*> nearFieldVehicleModel;
    std::unordered_map<uint64_t, AmbientPedModel*> nearFieldPedModel;
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
        bool isGenki = false;  // the original parked car: never despawned (CHOSEN, keeps the mission/smoke car alive)
    };
    std::vector<ParkedVehicle> parkedVehicles;
    uint64_t nextParkedId = 0;
    bool rigCarIsGenki = true;
    int scriptEnterAmbientAt = -1;  // --script-enter-ambient-at=FRAME (--no-input): warp beside nearest ambient car, E, drive, E
    int scriptEnterAmbientDriveFrames = 90;  // --script-enter-ambient-drive-frames=N
    int scriptEnterAmbientCount = 1;         // --script-enter-ambient-count=N cycles (each carjacks the nearest ambient car)
    bool scriptEnterAmbientReenter = false;  // --script-enter-ambient-reenter: re-enter the car just left, then exit
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
                            double z, double referenceY, double& outY);
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
void stepVehicleControls(PlaytestSession& sess, PlaytestFrame& frm);
void stepWorld(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficSignals(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficVehicles(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficPeds(PlaytestSession& sess, PlaytestFrame& frm);
void stepTrafficRoadblock(PlaytestSession& sess, PlaytestFrame& frm);
void stepNotoriety(PlaytestSession& sess, PlaytestFrame& frm);
void notorietyReportCrime(PlaytestSession& sess, const sr3airuntime::CrimeActivity& activity, const char* what);
void stepPlayerView(PlaytestSession& sess, PlaytestFrame& frm);
void stepMissionBridge(PlaytestSession& sess, PlaytestFrame& frm);
void stepMissionDrive(PlaytestSession& sess, PlaytestFrame& frm);
bool stepRender(PlaytestSession& sess, PlaytestFrame& frm);

// ---- end-of-run trace printing / cleanup ----
void reportPlaytest(PlaytestSession& sess);
// Save/load through sr3saveflow (viewer_playtest_saveflow.cpp).
void applyLoadedSave(PlaytestSession& sess);
void writeSaveAtExit(PlaytestSession& sess);

} // namespace sr3viewer
