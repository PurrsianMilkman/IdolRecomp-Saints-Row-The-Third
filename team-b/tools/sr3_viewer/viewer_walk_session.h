#pragma once

// Shared state for the `sr3_viewer walk` command (runWalkDemo()).  runWalkDemo() used to be one
// ~1,700-line function; its locals that are shared between the setup sections, the frame loop and the
// end-of-run report now live in WalkSession (whole-run state, members in the order the old locals were
// declared, so destruction order is unchanged) and WalkFrame (per-frame locals shared between the
// frame-loop steps).  Members that needed a non-trivial constructor/initialiser are std::optional<T>
// named `<name>_` (emplaced at the point the old local was declared); each setup/step function binds
// `auto& name = ...` references at its top so the moved code reads exactly as it did inside
// runWalkDemo().  See the walkSetup*()/walkFrame*() functions in viewer_walk_*.cpp.

#include "viewer_platform.h"
#include "viewer_modes.h"
#include "viewer_city_support.h"
#include "viewer_common.h"
#include "viewer_skinning.h"
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
#include "sr3player/ground_follow.h"
#include "vpp/container.h"
#include "sr3tables_environment/tables.h"
#include "sr3xtbl/xtbl.h"
#include "sr3playeranim/bridge.h"
#include "sr3animpose/clip_pose_bridge.h"
#include "sr3animpose/pose_compose.h"
#include "sr3tables_animation/tables.h"
#include "sr3player/camera_free_seed.h"
#include "sr3player/character_collision.h"
#include "sr3player/seated_ride_pose.h"

#include <functional>
#include <optional>

namespace sr3viewer {

// These four were function-local structs inside runWalkDemo(); they are namespace-scope now only
// because the session members below need to name them.
struct WalkRootMotion : sr3playeranim::ClipRootMotionSource {
    sr3player::Vec3 translationPerSecond(int32_t, bool loop) override {
        // SYNTHETIC stand-in (bridge.h's own documented precedent - this
        // project never decoded the .anim_pc keyframe payload, so there
        // is no real per-clip root motion anywhere here). A looping
        // locomotion clip (phase-synchronised-proxy via the loop bit)
        // advances at a CHOSEN constant 2.5 units/s; a one-shot clip
        // contributes none.
        return loop ? sr3player::Vec3{0, 0, 2.5} : sr3player::Vec3{};
    }
};

struct WalkScriptSource : sr3player::MoveInputSource {
    float A = 0.0f, B = 0.0f;
    float walkAxisA() override { return A; }
    float walkAxisB() override { return B; }
    bool gamepadActive() override { return true; }
    bool held(sr3player::MoveAction, int*) override { return false; }
    bool pressed(sr3player::MoveAction, int*) override { return false; }
    void consume(sr3player::MoveAction) override {}
    bool optionBit0() override { return true; }
};

struct WalkScriptStamina : sr3player::SprintStamina {
    void notSprintingStep(sr3player::PlayerMovementState&) override {}
    bool sprintingStep(sr3player::PlayerMovementState&) override { return true; }
    double available() override { return 1.0; }
    void startPenalty() override {}
    void jumpPenalty() override {}
    void step9ff270() override {}
};

struct FixedLookSource : sr3player::LookInputSource {
    float rotate() override { return 0.0f; }
    float elevate() override { return 0.0f; }
    bool cameraDrivenByKbm() override { return false; }
    bool invertHorizontal() override { return false; }
    bool invertVertical() override { return false; }
};

struct WalkSession {
    WalkSession() = default;
    WalkSession(const WalkSession&) = delete;
    WalkSession& operator=(const WalkSession&) = delete;

    // --- walkSetupArgs ---
    std::string charactersPath;
    std::string miscTablesPath;
    std::vector<std::string> cityArchivePaths;
    bool noInput = false, tintMode = false, textures = true, outlines = true;
    uint32_t winWidth = 1400, winHeight = 900; // same defaults runCity() uses
    int maxFrames = 80;                        // the scripted sequence's own total (see doc comment)
    int captureFrame = 79;                     // last frame, fully stopped
    std::string capturePath;
    std::vector<int> captureFrames; // --capture-frames (v2): empty = old single-capture behaviour
    bool v2 = false;                // --v2: see runWalkDemo()'s own "V2" doc block and the usage text
    bool collisionEnabled = false;  // --collision: see runWalkDemo()'s own doc comment and usage text
    // --head-matlib-attach (2026-10-08, spec-tables-customization.md Sec12.1a/12.1b
    // follow-up - supersedes the prior --experimental-head-matlib heuristic): see the
    // long doc comment at this flag's own use site (walkSetupCharacterHead(), inside the
    // `if (v2)` head block) for the full citation. OFF BY DEFAULT, v2-only; resolves+attaches brad's
    // matlib as its own identity-keyed, type-0x16 resource and logs it - does NOT bind
    // any texture to any material slot (the real render-time D3D-binding step stays
    // OPEN per the spec, not invented here). Never changes rendered output; never
    // affects any frozen golden baseline.
    bool headMatlibAttach = false;
    // --head-matlib-bind (2026-10-08, spec-tables-customization.md Sec12.1f
    // real-bind follow-up, implies headMatlibAttach): see the long doc
    // comment at this flag's own use site (walkSetupCharacterHead()), and
    // sr3geometry::PickMatlibTextureByRoleSuffix()'s doc comment in
    // character_matlib.h, for the full CHOSEN-vs-CONFIRMED citation. OFF BY
    // DEFAULT, v2-only; CHANGES rendered output (the head's diffuse
    // texture) when passed. Never affects any existing frozen golden
    // baseline (none passes this flag).
    bool headMatlibBind = false;
    int walkForwardFrames = 40, walkTurnFrames = 20;  // --walk-forward-frames/--walk-turn-frames (default matches every existing golden)
    std::string animArchiveOverride; // --anim-archive (FIX 4, --v2 only): see that block's own comment for the derived default
    float eye[3] = {-960.0f, 250.0f, -700.0f}; // tests/golden/city_corner's own framing
    float yaw = 0.0f, pitch = -75.0f * 3.14159265f / 180.0f;
    float moveSpeed = 60.0f, farZ = 3000.0f;
    bool visible = false; // off by default - see sr3render/window.h's own doc comment

    // --- walkSetupBrad ---
    std::vector<uint8_t> charArchiveBytes;
    std::vector<uint8_t> cb, gb, rb, unusedSibling;
    std::vector<uint8_t> headCb, headGb;
    std::optional<sr3geometry::MaterialBlock> material_;
    std::optional<sr3geometry::GeometryBlock> geometry_;
    std::optional<sr3mesh::MeshBlock> mesh_;
    size_t channelIndex = 0, drawGroup = 0; // brad's own golden (tests/golden/brad) uses these defaults
    std::optional<sr3rig::Rig> rig_;
    std::optional<SkinningModeResult> skinModeResult_;
    bool useBonePalette = false;
    std::vector<uint32_t> parentIdx;
    std::vector<std::array<float, 3>> restMesh;
    std::vector<std::array<float, 3>> restRig;
    std::vector<sr3rig::Mat3x4> bindSkin;
    std::vector<sr3mesh::Vertex> verts;
    sr3render::VertexDuplication vertexDuplication;
    sr3rig::SkinningStats bindStats;
    std::vector<std::array<float, 3>> bindPositions;

    // --- walkSetupAnimRuntime ---
    std::vector<uint8_t> miscBytes;
    std::optional<vpp::Container> miscTop_;
    std::function<std::optional<std::string>(const std::string&)> miscRead;
    std::function<sr3xtbl::Document(const std::string&)> docOf;
    std::optional<sr3xtbl::Document> paramsDoc_;
    std::optional<sr3animruntime::ControlParameterRegistry> registry_;
    std::optional<sr3xtbl::Document> filtersDoc_;
    std::optional<sr3animruntime::ControlFilterTable> filters_;
    std::vector<std::string> states, actions, triggerNames;
    std::optional<sr3animruntime::StateActionRegistry> stateActions_;
    std::optional<sr3animruntime::TriggerRegistry> triggers_;
    std::optional<sr3xtbl::Document> listDoc_;
    sr3animruntime::NodeGraphLoadInputs loadIn;
    std::optional<sr3animruntime::NodeGraphLibrary> lib_;
    int32_t gi = 0;
    sr3animruntime::GraphInstancePool pool;
    sr3playeranim::PlayerGraphServices svc;
    WalkRootMotion motion;
    std::optional<sr3playeranim::PlayerAnimBridge> bridge_;
    RealClipRegistry clipRegistry;
    int32_t runRecord = -1, jogRecord = -1, wlkRecord = -1;
    std::unique_ptr<std::vector<uint8_t>> animArchiveBytes;  // keeps the container's backing bytes alive
    sr3animpose::LoopPhaseTracker loopPhase;  // the shared phase-synchronised clock, see clip_pose_bridge.h
    bool realPoseEnabled = false;

    // --- walkSetupPlayerAndCamera ---
    WalkScriptSource src;
    WalkScriptStamina stamina;
    std::optional<sr3player::PlayerMovement> pm_;
    sr3player::Vec3 heldFacing{0, 0, 1}; // CHOSEN initial facing (+z) - matches the fixed camera-row convention below
    FixedLookSource lookSrc;
    std::optional<sr3player::CameraSystem> followCam_;
    sr3player::NoHitCameraCollision followCamCollision;

    // --- walkSetupCityArchives ---
    std::vector<std::unique_ptr<CityArchive>> archives;
    std::vector<CityCell> cells;
    std::map<std::string, size_t> cellByName;

    // --- walkSetupWindowDevice ---
    std::string error;
    std::optional<sr3render::Window> window_;
    std::optional<sr3render::RenderDevice> device_;
    std::optional<sr3render::SwapChain> swapChain_;
    std::optional<sr3render::MeshRenderer> focusBox_;
    uint32_t depthWidth = 0, depthHeight = 0;
    std::optional<sr3render::MeshRenderer> characterRenderer_;
    const sr3render::VertexDuplication* dupPtr = nullptr;

    // --- walkSetupCharacterTextures / walkSetupCharacterHead ---
    std::optional<sr3render::MeshRenderer> characterHeadRenderer_;
    std::vector<sr3render::UploadedTexture> characterMaterialTextures;
    std::vector<ID3D11ShaderResourceView*> characterPerMaterialSrv;
    bool characterTextured = false;
    std::vector<sr3render::UploadedTexture> characterHeadMaterialTextures;
    std::vector<ID3D11ShaderResourceView*> characterHeadPerMaterialSrv;
    bool characterHeadTextured = false;
    bool haveHeadMesh = false;

    // --- walkSetupStreaming ---
    std::map<size_t, std::unique_ptr<CityLoadedCell>> loaded;
    size_t totalLoads = 0, totalEvicts = 0;
    std::map<std::pair<int, int>, std::unique_ptr<CityGroundTile>> groundTiles;
    std::optional<ClmeshTriangleCollisionWorld> collisionWorld_;
    std::optional<sr3player::CharacterProxyDriver> collisionDriver_;
    std::optional<RealCameraCollision> realCameraCollision_;
    std::function<void(size_t, int)> loadCell;
    std::vector<size_t> lastWanted;
    bool firstReconcile = true;
    std::function<void(int, float, float)> reconcile;

    // --- walkSetupLoopState ---
    LARGE_INTEGER freq{}, prevTime{};
    int frames = 0;
};

// Per-frame locals shared between the frame-loop steps (a fresh one is constructed each iteration,
// exactly like the block-scope locals it replaces).
struct WalkFrame {
    sr3player::MoveFrameInput animFrame;
    sr3animpose::PoseComposeResult poseResult;
    double headingNow = 0.0;
    float charWorld[16];
    float viewProjection[16];
};

// Each returns -1 to continue, otherwise the exit code runWalkDemo() must return.
int walkSetupArgs(WalkSession& sess, int argc, char** argv);
int walkSetupBrad(WalkSession& sess);
int walkSetupAnimRuntime(WalkSession& sess);
int walkSetupPlayerAndCamera(WalkSession& sess);
int walkSetupCityArchives(WalkSession& sess);
int walkSetupWindowDevice(WalkSession& sess);
int walkSetupCharacterTextures(WalkSession& sess);
int walkSetupCharacterHead(WalkSession& sess);
int walkSetupStreaming(WalkSession& sess);
int walkSetupLoopState(WalkSession& sess);

// Frame-loop steps (the resize/`break` handling and present/frame-count stay in runWalkDemo()).
void walkFrameInput(WalkSession& sess);
void walkFramePlayer(WalkSession& sess, WalkFrame& frame);
void walkFrameRender(WalkSession& sess, WalkFrame& frame);
void walkFrameCapture(WalkSession& sess, WalkFrame& frame);

}  // namespace sr3viewer
