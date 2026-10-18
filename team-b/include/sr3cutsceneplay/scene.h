// In-game cutscene playback: the scene model and the naming / id rules -
// spec-cutscene-camera-format.md "In-game cutscene playback, end to end
// (2026-10-14)" sections A-F (and section 10 for the camera sampler, which
// sr3cutscene::CameraScript already implements).
//
// The model holds what playback needs from a scene file. The reader in
// scene_reader.h fills the parts whose element names the specs state; the
// rest (the per-shot actor rows and the Character name) is filled by the
// caller until those names are specified (see HANDOFF-C.md).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sr3cutsceneplay {

enum class SceneKind : int { Designer = 0, Zscene = 1, Story = 2 };

constexpr double kFramesPerSecond = 30.0;

// One Animations/Animation record of a shot actor row (C.4).
struct ClipRow {
    std::string filename;     // authoring path, e.g. D:/.../05_IN_BIKINI_1.animx
    uint16_t startFrame = 0;
    uint16_t endFrame = 0;
    uint16_t overlapPrevious = 0;
    float maxWeight = 1.0f;
};

// A per-shot actor row (C.3).
struct ShotActorRow {
    std::string name;  // as written; "#PLAYER#" is the live player
    bool hideChar = false;
    bool dontReposition = false;
    std::optional<std::array<float, 3>> pos;  // Pos
    std::optional<float> orientYDegrees;      // OrientY (x pi/180 at load)
    std::vector<ClipRow> clips;
};

// An Audios/Audio row (D.4). Times are in frames as authored (/30 s).
struct AudioLine {
    std::string filename;          // "Audio Data N" in every shipped row
    double delayBeforeFrames = 0.0;  // from the start of the shot
    std::string subtitleKey;       // a `subtitle` table key
    double subtitleDurationFrames = 0.0;
    std::string lipsyncClip;
    int32_t lipsyncCharIndex = 0;  // 1-based shot actor index
};

// A shot or scene option (Options/Option): a name plus its value.
struct SceneOption {
    std::string name;  // e.g. "Fullscreen Bink Play", "Bink Play", "Bink Stop"
    std::optional<bool> enabled;
    std::optional<float> number;
    std::optional<std::string> text;
    bool preFire = false;  // fires one shot ahead (the per-option table is not in the spec)
};

struct SceneShot {
    std::string slateName;
    int32_t definedShotLength = 0;  // frames; the script shot is Defined + 1 frames
    std::optional<int32_t> animShotLength;
    std::vector<ShotActorRow> actors;
    std::vector<AudioLine> audio;
    std::vector<SceneOption> options;
};

// A scene-level Characters/Character record (C.2).
struct SceneCharacter {
    std::string name;
    std::string mesh;
    bool isAngel = false;
    std::optional<std::string> killbaneMesh;  // the Killbane alternate mesh
    bool speakingThug = false;
    bool amCribProp = false;
    int32_t cribPropId = 0;
};

struct Scene {
    std::string name;
    SceneKind kind = SceneKind::Story;
    std::optional<std::string> soundtrack;         // "GROUP : VALUE"
    std::optional<std::string> binkMovieFilename;  // authoring path
    std::optional<std::string> nextScene;
    std::optional<std::string> vintDocument;
    bool restorePlayerPos = false;
    std::vector<SceneCharacter> characters;
    std::vector<SceneOption> options;  // scene-level, dispatched at load
    std::vector<SceneShot> shots;
    // Manager state the spec names without giving its source.
    double tailHoldSeconds = 0.0;   // +0x2190
    bool completionScreen = false;  // set by the "Show Completion Screen" option
};

// ---------------------------------------------------------------------------
// Timeline (B.3, section 10)
// ---------------------------------------------------------------------------

// Shot start/end in seconds: shot k spans Defined_k + 1 frames. Duration =
// sum(Defined + 1) / 30 (not the XML sum of Defined).
struct ShotSpan {
    double start = 0.0;
    double end = 0.0;
};
std::vector<ShotSpan> ShotSpans(const Scene& scene);
double SceneDuration(const Scene& scene);

// ---------------------------------------------------------------------------
// Names and ids
// ---------------------------------------------------------------------------

// 32-bit FNV-1 of the lower-cased name (Wwise ids, C/D).
uint32_t WwiseId(const std::string& name);

constexpr const char* kSoundtrackEvent = "Cinematic_CTE";
constexpr const char* kPersonaSwitchGroup = "Cinematic_Persona";
constexpr const char* kStopEvent = "Stop_Cinematics";
constexpr const char* kZsceneUnmuteEvent = "UnMute_After_Zscene";
constexpr const char* kSoundObjectName = "Cutscene";

// "GROUP : VALUE" -> {GROUP, VALUE}: split at the first ':' dropping one
// character on each side (the scene reader's rule). nullopt when the text
// is not of that form.
struct SoundtrackSwitch {
    std::string group;
    std::string value;
    uint32_t groupId = 0;  // FNV-1(group)
    uint32_t valueId = 0;  // FNV-1(value), also the soundbank id
    std::string bankFile;  // lower(value) + ".bnk_pc"
};
std::optional<SoundtrackSwitch> ParseSoundtrack(const std::string& text);

// Base name after the last '\' or '/'.
std::string BaseName(const std::string& path);
// "videos\" + base name of BinkMovieFilename.
std::string BinkMoviePath(const std::string& binkMovieFilename);
// The package: "<name>.str2_pc", or "<name>_f.str2_pc" for a female player.
std::string ScenePackageName(const std::string& sceneName, bool femalePlayer);
// Clip name: base name, ".animx" removed, lower-cased.
std::string ClipName(const std::string& filename);
// The clip to play: for a female player (or the forcing option) "f_" + name
// when it exists, else the plain name.
std::string ResolveClipName(const std::string& filename, bool femaleOrForced,
                            const std::function<bool(const std::string&)>& clipExists);

// Scene-level actor naming (C.2): "Player"/"#Player#" is the player; in a
// Zscene a name that is also a live world object becomes "<name>_cs".
bool IsPlayerActorName(const std::string& name);
std::string SceneActorObjectName(const std::string& name, SceneKind kind, bool liveWorldObjectExists);
// Per-shot rows store "#PLAYER#" as "#PLAYER1#" (the local player's real human).
std::string ShotActorObjectName(const std::string& name);

// The mesh a scene character spawns with (C.2).
struct MeshContext {
    bool missionM21Active = false;
    bool missionSh04Active = false;
    bool killbaneAlternate = false;  // the engine's alternate-Killbane test
};
std::string CharacterMesh(const SceneCharacter& c, const MeshContext& ctx);

}  // namespace sr3cutsceneplay
