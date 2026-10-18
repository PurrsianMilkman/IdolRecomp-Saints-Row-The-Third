#pragma once

// lua_host_run's shared helpers - formerly the anonymous namespace of tools/lua_host_run.cpp, moved
// verbatim into tools/lua_host_run/*.cpp (named namespace luahostrun). Helper types and the few
// constants/globals used across files are defined/declared here; see each .cpp for the helpers' own
// doc comments. The includes, the fs alias and the using-declarations are the original file's own.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sr3asm/manifest.h"
#include "sr3lua/parser.h"
#include "sr3luahost/host.h"
#include "sr3luahost/hook_registry.h"
#include "sr3tables_cutscene/scene_table.h"
#include "sr3tables_progression/tables.h"
#include "sr3tables_vehicle_world/world_items.h"
#include "sr3tables_weapons/combat.h"
#include "sr3vehicleinfo/vehicle_entry.h"
#include "sr3vintdoc/vint_doc.h"
#include "sr3zone/zone_objects.h"
#include "vpp/container.h"

namespace fs = std::filesystem;
using sr3luahost::LoadedVintDocument;
using sr3luahost::bareGlobalRoster;
using sr3luahost::BareThreadTable;
using sr3luahost::confirmedHooks;
using sr3luahost::HookGroup;
using sr3luahost::hookGroupLabel;
using sr3luahost::hookGroupNote;
using sr3luahost::HookSpec;
using sr3luahost::Host;
using sr3luahost::loadTaggedRegistrationList;
using sr3luahost::RegisteredName;

namespace luahostrun {

// ---- support_util.cpp ----
std::vector<uint8_t> readFile(const fs::path& path);
std::string toLower(std::string s);
bool endsWithLuaExt(const std::string& lowerName);

// tools/lua_reconciliation_called_and_registered_1181.tsv's own columns:
// name, total_call_sites, distinct_scripts, prefix_group, cluster - the
// real, static, call-count-ranked priority list this tool cross-references
// its own runtime hit counts against (sanity check, not an expected exact
// match - see this tool's own summary output for why).
struct StaticRank {
    uint64_t totalCallSites = 0;
    uint64_t distinctScripts = 0;
};

std::unordered_map<std::string, StaticRank> loadStaticReconciliation(const std::string& path);
std::string stripLuaChunkLocationPrefix(const std::string& msg);
std::string sanitizeTsv(const std::string& s);

// One real row of tools/mission_package_per_container.tsv (§9.125's own
// census output - columns: manifest_archive, manifest_path_chain,
// manifest_name, container_name, entry_count, mission_lua_entry_name,
// mission_lua_stem, stem_covers_a_start_script, other_entry_type_names).
// Only the 4 fields this pass actually needs are kept.
struct MissionPackageRow {
    std::string containerName;
    std::string missionLuaEntryName;
    std::string missionLuaStem;
    bool coversStartScript = false;
};

std::vector<MissionPackageRow> loadMissionPackageRows(const std::string& path);
std::string extractNilGlobalName(const std::string& msg);

// ---- support_cache_tables.cpp: real cache tables installed into the host's EngineState ----
std::string installSceneTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installContinuousExplosionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installWeaponDefinitionTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installInventoryItemTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installSpawnRankHitPointsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installVehicleTypeClassTableFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);
std::string installZoneObjectsFromCache(const fs::path& cacheDir, sr3luahost::EngineState& es);

// ---- support_archive_walk.cpp: the .lua/.vint_doc archive walk and the per-script state tags ----
struct ScriptInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string content;
};

struct WalkStats {
    uint64_t entries = 0;
    uint64_t containersOpened = 0;
    uint64_t decompressFailed = 0;
};

// A real .vint_doc leaf found by the same walk (2026-10-03): loaded into the
// UI engine state's Vint element tree on demand (see the document-context
// resolver in main()).
struct DocInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::vector<uint8_t> bytes;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
    std::vector<DocInstance> docs;
    WalkStats stats;
};

void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain);

namespace statetag {

enum class Tag { Ui, Gameplay, Open, Conflict };

const char* tagLabel(Tag t);

struct Index {
    std::unordered_map<std::string, Tag> byKey; // key: lower(containerName) + "|" + lower(entryName)
    uint64_t manifestsParsed = 0;
    uint64_t manifestParseFailures = 0;
    uint64_t asmInstancesFound = 0;
    uint64_t uiCandidateEntries = 0;      // real kind-21/22 + type-27 entries seen (population, not distinct)
    uint64_t gameplayCandidateEntries = 0; // real kind-39/40 + type-32 entries seen (population, not distinct)
    uint64_t conflicts = 0; // distinct (container,entry) keys where two manifests disagreed on tag - real, reported, not hidden
};

Index build(const std::vector<fs::path>& archives);

Tag resolve(const Index& idx, const std::string& pathChain, const std::string& entryName,
            std::string* containerNameOut = nullptr);

} // namespace statetag

// ---- support_mission_runner.cpp: watchdog hook, trace rings, --probe-autopilot capture, _start runner ----
// Real, unmodified Lua 5.1 debug-API count hook (lua_sethook +
// LUA_MASKCOUNT) - a local twin of Host::fireConfirmedHooks' own
// watchdogHook (src/lua_host.cpp, anonymous namespace there, not exposed
// to this file) for this NEW pass's own two NEW call sites (a mission
// script's own top-level runChunk() and its own
// `<stem>_start(checkpoint, is_restart)` call) - neither was ever
// watchdog-wrapped before (runChunk()'s own doc comment in host.h
// deliberately never wraps it, so as not to perturb HANDOFF.md Sec9.113's
// already-independently-verified baseline numbers; that concern does not
// apply here since this is an entirely separate, new measurement pass).
// "So one runaway mission can't hang the whole pass" - this task's own
// instruction. Reuses the SAME CHOSEN instruction budget as host.h's own
// kHookWatchdogInstructionBudget (5,000,000) for consistency - not an
// independently-chosen new number.
constexpr int kMissionWatchdogInstructionBudget = 5'000'000;

// --probe-autopilot (2026-10-07): CHOSEN, not a spec fact - how many
// EXTRA ticks (same per-tick shape as the main mission-drive loop: one
// fade/cutscene/clock frame + kSchedulerPassesPerTick scheduler passes
// each) to run after synthesizing a callback dispatch, looking for the
// first new error/wait-location-change/completion past it. Deliberately
// much smaller than the main --tick-budget=N (default 20, often raised to
// the hundreds to rule out a long legitimate wait) - a REAL blocker that
// actually was unblocked by the synthesized event should show up almost
// immediately on the very next resume; this is "does the mission move at
// all past this point", not "is this still mid-way through a long wait"
// (that question was already answered by the real --tick-budget loop
// BEFORE this mission was classified still_suspended in the first place).
constexpr int kProbeAutopilotFollowUpTicks = 30;

extern size_t g_traceRingSize; // --trace-host-calls-before-error=N - see support_mission_runner.cpp

// --probe-autopilot (2026-10-07): declared here (ahead of its own full doc
// comment, with g_firstResumeRegistrations/isCallbackRegistrationNative
// below) ONLY because resetTraceRing() needs it a few lines down - see the
// full doc comment at g_firstResumeRegistrations' own declaration, further
// below, for why this capture exists at all and what it holds.
struct CapturedRegistration {
    std::string nativeName;    // e.g. "on_trigger" - always one of isCallbackRegistrationNative's set
    std::string callbackName;  // arg 1 on every member of this family: the opaque callback-name string
    std::string targetName;    // arg 2 (trigger/character/vehicle name); empty for on_qte_animation_trigger,
                                // which per spec-lua-api-behaviour.md Sec10.7 is single-argument and global
};
extern std::unordered_map<lua_State*, std::vector<CapturedRegistration>> g_firstResumeRegistrations;
extern bool g_probeAutopilotEnabled; // set only by --probe-autopilot

void resetTraceRing(lua_State* co);
void printTraceRingIfAny(lua_State* co, const std::string& label, const std::string& errorMessage);
void printTraceRingStillSuspended(lua_State* co, const std::string& label);
void printTraceRingFirstResume(lua_State* co, const std::string& label);
void missionWatchdogHook(lua_State* L, lua_Debug* ar);
int missionWatchdogHookMask();
Host::RunResult runChunkWithWatchdog(Host& host, lua_State* L, const std::string& source,
                                      const std::string& chunkName);

// Real per-call outcome of watchdog-wrapped `funcName(checkpoint,
// is_restart)` - the SAME real shape as Host::HookFireResult
// (existedAsFunction/attemptedCall/callOk/callError), duplicated here
// (not added to Host's own public API - a mission-specific, 2-argument,
// non-hook call, genuinely different from fireConfirmedHooks' own
// zero-arg-by-default contract) rather than bent to fit an unrelated
// existing method.
struct MissionStartResult {
    bool existedAsFunction = false;
    bool attemptedCall = false;
    bool callOk = false;    // no error: the record finished, or yielded and is still alive
    bool suspended = false; // callOk and the record yielded (runner step 6: "alive")
    // When suspended: the record's id, so the mission loop can follow it
    // through the scheduler passes (BareThreadTable::schedulerPass).
    uint16_t recordId = BareThreadTable::kNoThread;
    std::string callError;
    // When suspended: the Lua source location that actually called thread_yield
    // (coroutine stack level 1 - level 0 is bareGuard's own C frame, where the
    // real lua_yield() call lives, src/lua_bare_globals.cpp; level 1 is the
    // mission script's own call site). Real, measured via lua_getstack/
    // lua_getinfo on the still-suspended coroutine, not inferred. Empty if not
    // suspended, or if neither stack level could be read.
    std::string suspendedAt;
};

MissionStartResult callMissionStart(lua_State* L, BareThreadTable& threads, const std::string& funcName,
                                     const std::string& checkpoint, bool isRestart);

} // namespace luahostrun
