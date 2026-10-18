#pragma once

#include "viewer_platform.h"
#include "viewer_city_support.h"
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
#include "sr3weapons/weapon_fire.h"
#include "sr3audioruntime/audio_output_device.h"
#include "sr3audioruntime/wav_writer.h"
#include "sr3audioruntime/wwise_vorbis_setup.h"
#include "sr3luahost/engine_state.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3luahost/minimap_live_transform.h"
#include "sr3luahost/host.h"
#include "sr3luahost/menu_runtime.h"
#include "sr3luahost/stub_registry.h"
#include "sr3radio/radio_player.h"

namespace sr3viewer {
extern const sr3zone::RoadGraph* g_playtestRoadGraph;

extern std::vector<sr3airuntime::RoadSpawnPoint> g_lastRoadSpawnPoints;

extern sr3airuntime::SegmentOccupancy g_roadOccupancy;

extern bool g_roadHighwayOnly;

// RG-3 (Sec20.6.1(c)): Spline_Type names of the streamed vehicle slots; the road-spawn include mask is built from
// them each gather. The viewer streams no vehicle-slot set (Sec20.6.1(e) re-roll cadence OPEN), so it is empty.
extern std::vector<std::string> g_streamedVehicleSplineTypes;

extern float g_frustumViewProj[16];

extern bool g_haveFrustum;

extern size_t g_roadVehGathers, g_roadVehCandidatesTotal, g_roadVehLaneSpawns;

extern size_t g_roadPedGathers, g_roadPedEdgesTotal;

std::vector<sr3airuntime::Vec3> GatherNearFieldSpawnCandidatesAroundPlayer(const sr3airuntime::Vec3& referencePos,
                                                                           const sr3airuntime::Vec3& cameraVelocity,
                                                                           sr3airuntime::DeterministicRng& rng);

std::vector<sr3airuntime::SidewalkSegment> GatherNearFieldPedSpawnCandidateSegmentsAroundPlayer(
    const sr3airuntime::Vec3& referencePos, double /*referenceHeadingRad*/, sr3airuntime::DeterministicRng& rng);


// One `.lua` leaf found by the walk below - trimmed to what stem resolution
// needs (tools/lua_host_run.cpp's own ScriptInstance also keeps archive/
// pathChain, for its own population-summary reporting this single-stem
// lookup has no use for).
struct MissionScriptInstance {
    std::string entryName;
    std::string content;
};

std::map<std::string, MissionScriptInstance> findScriptsByName(const std::vector<std::string>& archivePaths,
                                                                 const std::set<std::string>& wantedLower);

} // namespace sr3viewer

namespace sr3viewer {
namespace missionzoneobj {
std::string installVehicleTypeClassTableFromArchives(const std::vector<std::string>& archivePaths,
                                                      sr3luahost::EngineState& es);

std::string installZoneObjectsFromArchives(const std::vector<std::string>& archivePaths, sr3luahost::EngineState& es);

std::string installZsceneTableFromArchives(const std::vector<std::string>& archivePaths, sr3luahost::EngineState& es);

} // namespace missionzoneobj
} // namespace sr3viewer

namespace sr3viewer {
std::vector<std::string> expandArchivePathsWithCacheDirScan(std::vector<std::string> archivePaths);


// Real outcome of resolving `stem` against missionPackageTsvPath's rows,
// then finding (findScriptsByName above) a `.lua` leaf whose name case-
// insensitively equals that row's own mission_lua_entry_name.
struct MissionResolution {
    bool ok = false;
    std::string entryName;
    std::string containerName;
    std::string content;
    std::string error; // set iff !ok
};

MissionResolution resolveMissionStemScript(const std::vector<std::string>& archivePaths,
                                            const std::string& missionPackageTsvPath, const std::string& stem);


// The instruction-count watchdog - EXACT same mechanism/threshold as tools/
// lua_host_run.cpp's own missionWatchdogHook/kMissionWatchdogInstructionBudget
// (lua_sethook + LUA_MASKCOUNT, a real, unmodified public Lua 5.1 debug-API
// feature) - reused, not reinvented, per this task's own "don't invent new
// thresholds" instruction. Simplified relative to that file's own version:
// no trace-ring/probe-autopilot diagnostics (lua_host_run.cpp's own
// --trace-host-calls-before-error=/--probe-autopilot are that tool's
// separate, opt-in diagnostic tooling, out of this task's scope).
constexpr int kMissionWatchdogInstructionBudget = 5'000'000;

void missionWatchdogHook(lua_State* L, lua_Debug* /*ar*/);

sr3luahost::Host::RunResult missionRunChunkWithWatchdog(sr3luahost::Host& host, lua_State* L,
                                                         const std::string& source, const std::string& chunkName);


// MISSION-HOST PHASE 3 (this task): the hand-rolled watchdog-wrapped
// BareThreadTable::allocate/run pair that used to live here (MissionStartResult/
// missionCallStart(), Phase 1) is gone - the production bind-time block
// (runPlaytest(), below) now goes through the real sr3luahost::
// MissionPhaseMachine::triggerMarker() (include/sr3luahost/menu_runtime.h),
// which runs the identical mechanism (lua_getglobal -> BareThreadTable::
// allocate -> BareThreadTable::run, its own LaunchLuaCoroutine()) with its
// own matching 5,000,000-instruction watchdog budget
// (kCoroutineLaunchInstructionBudget, src/lua_menu_runtime.cpp) - not a
// second, divergent implementation kept in parallel.

//
// MISSION-HOST PHASE 1 (this task, 2026-10-09, branch feature/mission-host-
// phase1-debug-flag): per the approved-in-direction design doc (commit
// 775cd61 on branch design/mission-host-in-engine-loop,
// results/design_mission_host_in_engine_loop_20261009.md), this adds a NEW,
// opt-in `--mission=<stem>` debug flag that drives a real shipped mission
// Lua script, once per REAL rendered frame, through the SAME ONE
// sr3luahost::Host/EngineState this function already builds for --hud
// (hudHost/hudEs/hudGp below, now shared rather than --hud-only - see the
// "if (hudEnabled || missionEnabled)" block). Without --mission, nothing
// about this function changes (missionEnabled stays false, every new block
// below is skipped, byte-for-byte the same code path as before this task -
// verified explicitly, see this task's own HANDOFF/commit for the before/
// after ctest run).
//
// Resolution method (stem -> real script source) is copied and adapted,
// deliberately, from tools/lua_host_run.cpp's own WalkState/inspectLeaf/
// walk() (the ".lua"-suffix leaf test, the str2_pc nested-container-or-
// compressed-0x51890ACE-magic recursion) and its own MissionPackageRow/
// loadMissionPackageRows()/first-found-by-entry-name matching rule (that
// file's own mission-drive loop, lines ~1096-1130/~3204-3222 as of this
// extraction) - NOT refactored into a shared header: this project's own
// established convention (tools/lua_host_run.cpp's own top comment) is to
// duplicate a small recursive walk per tool, with attribution, rather than
// share one (same precedent as vpp_dump.cpp/vpp_extract.cpp each carrying
// their own copy) - followed here rather than inventing a different shape.
// The one real difference: lua_host_run.cpp walks every .vpp_pc in a whole
// cache DIRECTORY; this command has no such directory, only the specific
// archives its own argv already named (<characters.vpp_pc>/<misc_tables.
// vpp_pc>/<city archives>...) - so resolveMissionStemScript() below walks
// `cityArchivePaths` only (where every one of the 49 real mission_lua_stem
// entries tools/mission_package_per_container.tsv lists actually lives -
// confirmed directly: m01's own row names manifest_archive=sr3_city_0.vpp_pc,
// which this command's own first required positional argument already is;
// a DLC stem needs its own dlc1/dlc2/dlc3.vpp_pc passed as one of this
// command's own "[<more city archives>...]" tokens, same convention as
// every other archive this function already takes).
//
// Per-frame tick shape (screenFadeHostFrame -> cutsceneHostFrame ->
// gameClockAdvanceFrame -> ONE BareThreadTable scheduler pass ->
// fireConfirmedHooks) is the SAME CHOSEN order tools/lua_host_run.cpp's own
// mission-drive loop uses (that file's own kFadeHostMsPerTick/
// kSchedulerPassesPerTick=1 comment, re-confirmed against spec-render-
// pipeline.md §6.6 before this task started: Lua coroutine resume runs on
// the main thread once per rendered frame, matching this function's own
// real per-frame loop exactly) - the one real change is that this loop
// already has a REAL per-frame `dt` (QueryPerformanceCounter-measured, or
// the deterministic 1/30s --no-input substitute), so it is used directly in
// place of lua_host_run.cpp's own fixed 333ms/tick simulated-time
// substitute (design doc §3b: "the simulated-seconds tick budget... is
// dropped once real dt drives it" - this IS that real-dt caller). The
// instruction-count watchdog (lua_sethook+LUA_MASKCOUNT,
// kMissionWatchdogInstructionBudget=5,000,000) is kept EXACTLY as tools/
// lua_host_run.cpp's own missionWatchdogHook uses it (same threshold, same
// mechanism, reused not reinvented) - see that file's own doc comment for
// why it exists (an always-nil-returning stub's busy-poll loop must not
// hang the host). NEW for this task (design doc §6's own flagged risk,
// genuinely new design, not a reuse of anything that already existed): a
// SEPARATE wall-clock timeout, `--mission-timeout=<seconds>` (CHOSEN
// default 30.0, see kMissionDefaultTimeoutSeconds below), guards the
// INTERACTIVE loop itself - unlike lua_host_run.cpp's batch tool, a hung
// real-time session would otherwise stall actual frame delivery forever. No
// existing timeout convention was found anywhere else in this project to
// reuse (grepped tools/include/src for "imeout"/"TIMEOUT" before choosing
// this - the only hits are real per-field xtbl/game-timer values and one
// unrelated soundtrack-handshake constant in src/lua_cutscene.cpp, none of
// them a host/tool-level process timeout). Accumulated against the SAME
// per-frame `dt` this function's own loop already computes (real wall-clock
// time in normal operation; the SAME deterministic 1/30s substitute under
// --no-input that every other per-frame value in this loop already uses) -
// not a second, independent clock - so the timeout is both a faithful
// wall-clock bound live and exactly reproducible under ctest.
//
// Phase 2 (a live position bridge / name-to-live-object resolver so a
// mission script's `get_dist`/vehicle-seat-scoring natives see this loop's
// REAL player/vehicle positions) and Phase 3 (below) have both since landed
// on top of this Phase 1 scaffold - see EngineState::bridgeLiveCharacterPosition()/
// bridgeLiveVehiclePosition()/liveObjectKind() for Phase 2.
//
// MISSION-HOST PHASE 3 (this task, branch feature/mission-host-phase3-m05-
// vehicles): the bind-time block below now goes through the REAL
// sr3luahost::MissionPhaseMachine class (include/sr3luahost/menu_runtime.h)
// instead of EngineState::bindMissionPhaseMachine()'s one-shot collapse -
// bind()+triggerMarker() (Sec21.27a's own phase 8 -> 2 -> marker-trigger
// sequence), not a hand-rolled BareThreadTable::allocate/run pair any more.
// triggerMarker() itself still reaches EngineState::beginMissionPhaseTracking()
// (MissionPhaseMachine::launchStartFunction()'s own doc comment, menu_
// runtime.h) - the SAME legacy-facade write every native gated on
// MissionContext::activeAndRunning/etc. needs - so this switch is additive
// to the real marker/phase model, not a behaviour change for anything
// downstream of that call. EngineState::bindMissionPhaseMachine()/
// beginMissionPhaseTracking() are kept (NOT removed) because tools/
// lua_host_run.cpp's own standalone headless mission-drive loop is a
// legitimate SEPARATE caller of them (that file's own mission-drive loop,
// "host.engineState().beginMissionPhaseTracking(row.missionLuaStem, ...)")
// - this task only switches sr3_viewer.cpp's production playtest path, per
// this task's own dispatch.
// ---------------------------------------------------------------------
// FOOTSTEP-CADENCE SFX LOOKUP (audio-playback task, 2026-10-10)
// ---------------------------------------------------------------------
// Finds the first real, playable SFX clip this project's own CONFIRMED
// lookup mechanism can resolve out of a real `<archive>.vpp_pc`:
//   1. walk the archive's own entries for a plain `<stem>.bnk_pc` /
//      `<stem>_media.bnk_pc` sibling pair (spec-audio-format.md Sec3/Sec4;
//      sr3audio/hirc.h + media_bank.h - NOT reinvented here);
//   2. walk the plain bank's HIRC chunk (sr3audio::walkHircItems) for a
//      kind-2 Sound item whose source descriptor classifies kPlainCodec
//      (spec-audio-runtime.md Sec9.8's "ordinary one-shot SFX" shape);
//   3. resolve it to a real media record via
//      sr3audio::resolveSoundIdToMedia() (the HIRC id -> R3 -> `_media.bnk_pc`
//      tag match Sec9.8's own worked example establishes);
//   4. attempt the real Vorbis decode (sr3audioruntime::attemptWwiseVorbisDecode,
//      the same, already independently oracle-verified pipeline - see
//      HANDOFF.md's 493/493 cutscene_sounds.vpp_pc citation).
// Returns on the FIRST Sound item that resolves AND decodes successfully;
// does not try to pick a semantically meaningful one.
// UPDATE (footstep-real-event task, 2026-10-10): spec-audio-runtime.md
// Sec11a.8 (landed after the original audio-playback task above) DOES now
// confirm a real footstep-specific SFX identity: the literal Wwise event
// string "FOOTSTEP", sourced from anim_files.xtbl Sounds/Sound rows (144 in
// the base game), each pairing a MATERIAL_TYPE switch with a SHOE_TYPE/
// MOVEMENT_TYPE switch. runPlaytest() now looks that identity up by name
// (see its own "REAL FOOTSTEP event identity + CHOSEN switch selection"
// block) using MATERIAL_TYPE:CONCRETE+SHOE_TYPE:SNEAKER (CHOSEN, per
// purrsian-52, for open-world walking which has no author-baked material
// context). What is STILL genuinely open, and why this function itself is
// UNCHANGED: resolving that named event+switch identity to an actual Wwise
// media id requires walking Event(kind4)->Action(kind3)->SwitchContainer,
// and no switch-container HIRC on-disk layout is reverse-engineered
// anywhere in this project (spec-audio-runtime.md Sec9.6/9.7/9.8 document
// Event->Action->plain-Sound only, never a switch-keyed container) -
// guessing that binary layout would be exactly the kind of invented engine
// behaviour the no-invented-fixes rule forbids. So this function still
// returns the nearest REAL, independently decode-verified SFX media this
// project can point to (by raw HIRC id, not by the real event name) -
// NOT a claim that the specific clip played IS the real FOOTSTEP media,
// but now paired with an honest, explicit statement (in runPlaytest()'s own
// log output) of the real identity that could not yet be resolved to it.
// The result is played when the real left-foot/right-foot anim-clip
// Trigger (Sec11a.8 again) fires (or, lacking that data, a footstep-shaped
// CADENCE - see runPlaytest()'s own call site for which) as the
// first concrete end-to-end proof that decoded PCM reaches a real speaker,
// not as a claim about which game sound is now correctly audible.
struct FootstepCadenceSfxClip {
    bool valid = false;
    uint32_t soundObjectId = 0;
    std::string bankStem;
    sr3audioruntime::DecodeAttemptResult decoded;
};

FootstepCadenceSfxClip findFirstPlayableSfxClip(const std::string& archivePath, std::string& diagnosticLog);

std::unique_ptr<sr3zone::RoadGraph> LoadPlaytestRoadGraph(const std::vector<std::unique_ptr<CityArchive>>& archives,
                                                           std::vector<sr3zone::HoodVolume>* hoodsOut = nullptr);


// --auto-combat harness damage sink (CHOSEN test-harness wiring): a real hitscan hit from the player's weapon
// (sr3weapons::WeaponInstance::fire -> CastHitscanRay -> applyLethalHit) is routed into the real damage model
// (EngineState::applyDamageModelHit, attacker "#PLAYER1#") with a CHOSEN per-hit amount instead of the
// guaranteed-lethal shape, so a target dies through the normal death path (finalizeLethalCharacter -> on_death).
class AutoCombatDamageSink : public sr3weapons::IWeaponDamageSink {
public:
    AutoCombatDamageSink(sr3luahost::EngineState& es, float amount) : es_(es), amount_(amount) {}
    void applyLethalHit(const std::string& targetName) override {
        try {
            const auto r = es_.applyDamageModelHit(targetName, "#PLAYER1#", amount_, 0u);
            if (r.applied) { ++applied; damageDealt += r.damage; } else { ++notApplied; }
            if (r.killed) ++killsByHit;
        } catch (const sr3luahost::OpenStateError&) {
            ++refused;  // the target's hit points are an OPEN value: no damage invented
        }
    }
    float amount() const { return amount_; }
    uint64_t applied = 0, notApplied = 0, refused = 0, killsByHit = 0;
    double damageDealt = 0.0;
private:
    sr3luahost::EngineState& es_;
    float amount_;
};

} // namespace sr3viewer
