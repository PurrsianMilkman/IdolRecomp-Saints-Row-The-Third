// Free-roam load and save flow - spec-save-format.md "Free-roam load and
// save flow (2026-10-14)" A.1-A.4, plus the directory write rule of 6.1 and
// the menu-save checks of 15.1. Pure logic over snapshot images: the world
// (object lookup, player-start marker, crib transforms) is reached through
// callbacks, so this builds and tests without an engine.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sr3save/save_directory.h"
#include "sr3saveflow/snapshot_blocks.h"

namespace sr3saveflow {

constexpr const char* kCityZone = "sr3_city";
constexpr uint32_t kSnapshotVersion = 94;

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

// Position (object +0x40) and 3x3 orientation (object +0x4C).
struct WorldTransform {
    Vec3 position;
    std::array<float, 9> orientation{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

// ---------------------------------------------------------------------------
// Load (A.1)
// ---------------------------------------------------------------------------

enum class LoadKind {
    Full,        // a slot or Continue: placement and play time restored
    // A mission checkpoint (the same snapshot layout in memory, 11.2 / 15.3):
    // play time keeps running (A.1 entry 0), no placement. Its caller is the
    // mission-checkpoint restore, which this library does not host.
    Checkpoint,
};

// What the world answers during the apply. Every callback may be empty.
struct LoadWorld {
    // The named mission-start object "tss01" (branch (a)): its handle and
    // transform, when it exists. Shipped data never has it.
    std::function<std::optional<std::pair<ObjectHandle, WorldTransform>>(const std::string&)>
        findMissionStart;
    // Resolve a handle to a live, enabled object of the expected class.
    std::function<std::optional<WorldTransform>(const ObjectHandle&)> resolveHandle;
    // The single registered player-start marker (first registration wins).
    std::function<std::optional<WorldTransform>()> playerStartMarker;
};

enum class PlacementSource {
    None,             // not placed (checkpoint, guest, or the version gate failed)
    MissionStart,     // branch (a): "tss01"
    RespawnCrib,      // branch (b): header handle resolved
    PlayerStartMarker // branch (b) fallback: handle zero or unresolved
};

struct Placement {
    PlacementSource source = PlacementSource::None;
    ObjectHandle handle;                     // the object placed at, when known
    std::optional<WorldTransform> transform; // empty when the source had none
};

// One step of the load, in the order the game runs them.
enum class LoadStepKind {
    StreamingWait,       // the zone streams before any save data is applied
    SubsystemResets,     // ~50 resets, respect multiplier := 1.0 among them
    VersionGate,         // version 94, else the driver is skipped silently
    ApplyEntry,          // entries 0..31 ascending (entry in LoadStep::entry)
    LoadFinished,
    PostStartStreamingWait,  // around the start point (whether it runs is OPEN)
};

struct LoadStep {
    LoadStepKind kind;
    int entry = -1;
    const char* what = "";
};

struct LoadPlan {
    std::string zoneName;
    bool versionOk = false;  // false: nothing below "applied" was applied
    std::vector<LoadStep> steps;

    // entry 0
    GameClock clock;                 // restored verbatim (date and time of day)
    std::optional<double> playTime;  // float(u32 @0x0A0) on a full load; empty on a checkpoint
    std::array<uint8_t, kFlagSetBytes> flagSet{};
    Placement placement;
    // The point the post-start streaming wait centres on: the explicit
    // placement when there is one, else the registered player-start marker.
    std::optional<Vec3> streamCentre;

    // entry 1
    int32_t cashRaw = 0;  // through the clamp
    uint32_t respectTotal = 0, respectInLevel = 0, respectLevel = 0;  // verbatim
    float respectMultiplier = 1.0f;  // reset to 1.0, then 0x4A5C unconditionally

    // entry 9
    CribBlock cribs;
    // entry 22: the takeover regions' controlled bits (record flag bit 0)
    std::vector<bool> regionControlled;
    uint32_t difficulty = 1;

    // Not saved, so a load starts them fresh: no notoriety, on foot, and
    // the weather system's own default (which default is OPEN).
    static constexpr float kNotorietyAfterLoad = 0.0f;
    static constexpr bool kInVehicleAfterLoad = false;
};

struct LoadOptions {
    LoadKind kind = LoadKind::Full;
    // Placement runs only with no network session or as the session host.
    bool sessionHostOrNoSession = true;
    // Branch (a) is dead in shipped data; a host without a live mission-object
    // table may skip it.
    bool tryMissionStartOverride = true;
};

// Builds the load plan for a full snapshot image. Throws sr3save::FormatError
// for an image the reader refuses.
LoadPlan PlanFreeRoamLoad(const std::vector<uint8_t>& image, const LoadWorld& world,
                          const LoadOptions& options = {});

// ---------------------------------------------------------------------------
// Save (A.2)
// ---------------------------------------------------------------------------

struct CribLocation {
    ObjectHandle handle;
    bool locked = true;
    Vec3 position;
};

// The nearest owned (unlocked) crib by straight-line distance to the
// player; zero when the zone is not sr3_city or no crib is owned.
ObjectHandle NearestOwnedCrib(const std::string& zoneName, const Vec3& player,
                              const std::vector<CribLocation>& cribs);

// The live state a free-roam save writes over its base image.
struct FreeRoamSaveState {
    std::string zoneName = kCityZone;
    Vec3 playerPosition;
    std::vector<CribLocation> cribPositions;  // live crib objects (for the respawn crib)
    GameClock clock;                          // its playTime is the running play float
    int64_t cashRaw = 0;
    uint32_t respectTotal = 0, respectInLevel = 0, respectLevel = 0;
    float respectMultiplier = 1.0f;
    CribBlock cribs;
};

// Writes the state over `image` (a full snapshot image: the loaded save, or
// a new-game image) and stamps the hash. Every span the state does not
// cover is left as it was. No player position is written anywhere: 0x058
// gets the nearest owned crib, or zero.
void PatchFreeRoamSave(std::vector<uint8_t>& image, const FreeRoamSaveState& state);

// ---------------------------------------------------------------------------
// When free roam can save (A.2, 15.1, 15.2)
// ---------------------------------------------------------------------------

struct PauseMenuContext {
    bool hordeMode = false;
    bool machinimaRecording = false;
    bool autosaveInProgress = false;
    bool inMission = false;   // a mission, silent mission or stronghold
    bool inActivity = false;  // an activity or an active diversion
    bool missionWarningShown = false;
    bool activityWarningShown = false;
};

enum class PauseSaveOutcome {
    NotOffered,         // horde mode or machinima: no Save/Load entries
    RefusedAutosaving,  // SAVELOAD_CANT_ENTER_AUTOSAVE
    MissionWarning,     // SAVELOAD_MISSION_WARNING, then proceed
    ActivityWarning,    // SAVELOAD_ACTIVITY_WARNING, then proceed
    Proceed,            // to the slot screen
};

// The warnings show once (the caller records that it showed one and asks
// again to proceed). Vehicles, notoriety and combat never refuse.
PauseSaveOutcome PauseMenuSave(const PauseMenuContext& ctx);

enum class SlotSaveOutcome {
    NotEnoughSlots,  // SAVELOAD_NOT_ENOUGH_SLOTS_NEW
    NotEnoughSpace,  // SAVELOAD_NOT_ENOUGH_SPACE_NEW
    ConfirmOverwrite,  // SAVELOAD_SAVE_GAME / SAVELOAD_SAVE_GAME_CONFIRM_EXPOSITION
    Save,
};
// newSlot: the "new save" entry; otherwise an occupied slot was picked.
SlotSaveOutcome SlotScreenSave(bool newSlot, bool slotAvailable, bool spaceAvailable);

enum class AutosaveSource {
    GameAutosave,     // building purchase, finished collection
    MissionAutosave,  // mission scripts; bypasses the mission gate
};

struct AutosaveGate {
    bool startupFinished = true;
    bool alreadySaving = false;
    bool missionActive = false;
    bool sessionBlocked = false;  // the two further session/menu checks
    bool menuBlocked = false;
};
bool AutosaveAllowed(AutosaveSource source, const AutosaveGate& gate);

// ---------------------------------------------------------------------------
// New game (A.3)
// ---------------------------------------------------------------------------

// The spec's recommended starting cash ($57.00, the earliest observed
// value); engine-like behaviour may start at 0 (HYPOTHESIS).
constexpr int32_t kRecommendedStartingCashRaw = 5700;

struct NewGameOptions {
    GameClock baseClock;  // the live clock's date; time is overwritten
    int32_t startingCashRaw = kRecommendedStartingCashRaw;
    uint32_t difficulty = 1;
};

// A full new-game snapshot image: zone sr3_city, 10:00:00, play time 0,
// respect 0/0/0, multiplier 1.0, the ten cribs locked with the "cribs"
// variable 0, no respawn crib (the next load uses the player-start marker).
std::vector<uint8_t> BuildNewGameSnapshot(const NewGameOptions& options = {});

// ---------------------------------------------------------------------------
// Directory (6.1)
// ---------------------------------------------------------------------------

// The slot record a snapshot is listed under: active, with the summary the
// game derives from the snapshot.
sr3save::SlotRecord MakeSlotRecord(const std::vector<uint8_t>& snapshotImage, bool autosave);

// A 608-byte savedir.sr3d_pc: records that are inactive or excluded are
// zeroed whole, only the survivors are counted into 0x004, and the CRC of
// 0x008..0x25F goes at 0x000.
std::vector<uint8_t> BuildSaveDirectory(const std::array<sr3save::SlotRecord, sr3save::kSlotCount>& slots,
                                        const std::array<bool, sr3save::kSlotCount>& excluded = {});

}  // namespace sr3saveflow
