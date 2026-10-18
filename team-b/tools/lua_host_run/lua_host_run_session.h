#pragma once

// Shared state for lua_host_run's main(). main() used to be one ~2,570-line function; its locals that are
// shared between its sections now live in LuaHostRunSession (members in the order the old locals were
// declared, so they are destroyed in the same order). The one member that needed a constructor argument is
// std::optional<Host> host_ (emplaced at the point the old local was declared); `hooks` (a reference local)
// is the pointer hooks_. Each section function binds `auto& name = sess.name;` references at its top so the
// moved code reads exactly as it did inside main(). Declarations whose initialiser was not a literal became
// plain assignments at the original point.

#include "lua_host_run_support.h"

namespace luahostrun {

// ---- constants that were `const`/`constexpr` locals of main() ----
constexpr size_t kProbeAutopilotAutoRingSize = 200;

// CONFIRMED, not chosen (spec-lua-api-behaviour.md Sec36.4, spec-lua-bindings.md
// Sec14.11, synced 2026-10-02): a genuine fresh start's two native call sites
// always push the literal string "mission start" and the boolean false - see
// callMissionStart()'s own doc comment for the full citation and for why this
// harness (fresh-start-only, no resume/retry call site) never needs any other
// checkpoint-name value. Previously this harness passed the CHOSEN number 0.0
// here, which reproduced the real dlc2_m02 "attempt to index local 'cp_data'
// (a nil value)" bug: `checkpoints[0]` is nil when a mission only ever keys
// its checkpoint table by name strings.
const std::string kMissionStartCheckpoint = "mission start";
constexpr bool kMissionStartIsRestart = false;  // CONFIRMED false on a fresh start (Sec36.3)
// Screen fade host clock (batch 2026-10-01, spec-lua-api-behaviour.md
// Sec26.24): each mission tick is one host frame of this many ms
// (EngineState::screenFadeHostFrame). CHOSEN: about a third of a second,
// and not a divisor of the engine's 1000/1500/3000/6000 ms stamp offsets,
// so a stamp never lands exactly on a frame (that comparison is OPEN).
constexpr int64_t kFadeHostMsPerTick = 333;
// Script-thread scheduler cadence (spec-lua-api-behaviour.md thread-table
// section, "MAJOR CORRECTION (2026-10-02, stuck-delay-mission
// investigation)", CONFIRMED - disassembly, re-derived a second time -
// supersedes the "[RESOLVED 2026-10-02 ...]" pass immediately before it,
// which this one corrects rather than contradicts): 0x00702070 is NOT a
// steady background pump - it's the boot-only "Boot_render" loading-
// screen thread, which terminates before the first gameplay frame and
// never runs again. The real steady-state driver is 0x00e0dfc0, on the
// MAIN thread, called once per rendered/game frame from each active game
// state's own per-frame update (0x007b05f0 for the in-game state, the
// front-end state, and the cutscene driver 0x00702a50), which itself
// unconditionally calls the scheduler 0x00e0cf50 once per frame (no
// budget - every live, non-exempt record is offered a resume on that one
// call) - except while 0x00dafa90 reports a blocking stream load in
// progress (not modelled here; this host never simulates a blocking
// load). There never was an independent ~30 ms pump to approximate
// outside the boot screen; the spec states plainly that a host which
// "resumes 0x00e0cf50 once per rendered frame ... reproduces the real
// engine exactly."
//
// CHOSEN (engineering decision, not spec fact - supersedes the old
// floor(333 ms / 30 ms) = 11-passes-per-tick mapping this comment
// previously derived from the now-corrected "independent ~30 ms pump"
// reading): this host already treats one mission tick as one host frame
// (kFadeHostMsPerTick above - "each mission tick is one host frame of
// this many ms"), and the real mechanism is now confirmed to run exactly
// once per rendered/game frame, so the faithful mapping is 1:1 - one
// scheduler pass per tick. The old 11x figure was sized to approximate a
// ~30 ms cadence that the corrected spec text confirms never existed
// outside the boot screen; running it that many times no longer has any
// engine cadence behind it, so it is simplified away rather than kept as
// a "safe superset" (nothing here is being approximated anymore - this
// IS the mechanism, 1 pass per frame, not a bound on it).
constexpr int kSchedulerPassesPerTick = 1; // CHOSEN: 1 host tick == 1 rendered/game frame == 1 scheduler pass

// Globals the host registers beyond the tagged list: the 24 bare globals of
// 0x00e0f900 (spec-lua-bindings.md Sec13.2) and the scaffold's thread_close.
const size_t kHostExtraGlobals = 24 + 1;

// ---- local helper types of main(), moved here verbatim ----
struct PreloadResult {
    std::string name;
    std::string stateLabel;
    size_t instancesFound = 0;
    bool loadOk = false;
    bool pcallOk = false;
    std::string loadError, pcallError;
};

struct HistHit {
    uint64_t callCount = 0;
    int lastArgc = 0;
    std::string lastArgTypes;
};

struct HookAgg {
    uint64_t gpDefined = 0, gpCalledOk = 0, gpCalledErr = 0;
    uint64_t uiDefined = 0, uiCalledOk = 0, uiCalledErr = 0;
};

struct MissionResult {
    std::string stem, entryName, containerName;
    bool scriptFound = false;
    bool loadOk = false, pcallOk = false;
    std::string loadError, pcallError;
    bool startExisted = false, startAttempted = false, startCallOk = false;
    bool startSuspended = false; // _start's record yielded (no error) and is still alive
    std::string startSuspendedAt; // see MissionStartResult::suspendedAt
    std::string startCallError;
    // What became of a suspended _start record over the tick loop's
    // scheduler passes: "" (not suspended), still_suspended, finished
    // (resumed and ran to its end), killed (released by the runner's step
    // 2), errored (its own Lua error, in startAfterResumeError).
    std::string startAfterTicks;
    std::string startAfterResumeError;
    std::string startStillSuspendedAt; // still_suspended: where it is now waiting
    uint64_t schedulerResumes = 0;      // records this mission's passes resumed
    uint64_t schedulerThreadErrors = 0; // this mission's records that raised on a resume
    uint64_t schedulerForeignResumes = 0; // resumes of records left over from earlier missions
    int ticksSurvived = 0;
    std::string stopReason;
    std::string firstErrorMessage;
    std::string firstUnimplementedStub;
    // --probe-autopilot (2026-10-07) fields - all left at their default
    // (empty/false) unless --probe-autopilot was given AND this mission
    // was classified still_suspended above; see that flag's own usage
    // text and the classification-site block below for how they're
    // filled. Purely additive reporting - never read by, or fed back
    // into, any of the REAL fields above.
    bool probeAutopilotAttempted = false;
    bool probeAutopilotHadCallback = false;
    std::string probeAutopilotNative;         // e.g. "on_trigger"; empty if nothing to synthesize
    std::string probeAutopilotCallbackName;   // the recovered callback-name argument
    std::string probeAutopilotTargetName;     // the recovered trigger/character/vehicle name (may be empty)
    std::string probeAutopilotDispatchResult; // outcome of calling the callback-name global once
    std::string probeAutopilotNextBlocker;    // first new error/wait/completion found in the follow-up window
    std::string probeAutopilotNote;           // set to the "nothing to synthesize" finding when applicable
    // THE headline measurement this task (2026-10-08, Sec21.27a/
    // Sec21.27b) adds: did THIS mission's own mission handle actually
    // reach the real completion state - EngineState::
    // missionCompletionFlagBitSet(), the real `+0x88` bit 0x4
    // (Sec15.10/Sec21.27b's own writer, 0x006d6870) - by the time this
    // mission's own tick loop stopped (budget exhaustion or the
    // idempotent-signature "completion" stop below)? Deliberately NOT
    // named `completion` or folded into stopReason above: stopReason's
    // own "completion" value only means "2 consecutive identical tick
    // signatures," a Lua-coroutine-idle measurement that pre-dates this
    // task and is NOT the same thing as the mission's own completion
    // flag - conflating the two is exactly the "no error" vs. "really
    // FINISHED" gap this task was asked to measure honestly.
    bool missionCompletionFlagSet = false;
};

// ---- state shared by main()'s sections ----
struct LuaHostRunSession {
    // --- parseArgs (original lines 1922-2166) ---
    bool preloadStatesSpec164 = true; // Sec16.1/Sec16.4, CONFIRMED and cleared 2026-10-01
    bool hostRng = false;      // --host-rng: only when explicitly given
    uint64_t hostRngSeed = 1;
    // --display (Sec26.26): CHOSEN default 1280x720, a host input (see usage).
    bool displayInit = true;
    int32_t displayWidth = 1280, displayHeight = 720;
    // --tick-budget (CHOSEN override, not a spec fact): how many mission-drive
    // ticks to run per mission before "budget exhaustion" - see
    // kMissionTickBudget's own doc comment below for what one tick models.
    // Default unchanged (20); a caller investigating whether a "still
    // suspended" mission is a real blocker or just a long, legitimate wait
    // (e.g. a scripted delay/fade) should pass a much larger value and read
    // start_still_suspended_at in the per-mission output / TSV to see where
    // it parked.
    int missionTickBudget = 20;
    // --probe-unresolved-names: DIAGNOSTIC ONLY, off by default (see
    // OpenValueMap::enableProbeFallback's own doc comment, open_state.h).
    // Never feeds a real baseline measurement - every number it influences
    // is a synthetic stand-in for the real, still-genuinely-OPEN named-
    // object resolution map, used only to see what blocks a mission NEXT
    // once that one gate stops refusing.
    bool probeUnresolvedNames = false;
    // --no-vint-docs: turn off real .vint_doc loading + the per-chunk
    // document context (2026-10-03, see the block after the archive walk).
    bool vintDocs = true;
    // --probe-autopilot: DIAGNOSTIC ONLY, off by default - see its own
    // usage text above and g_probeAutopilotEnabled's own doc comment.
    bool probeAutopilot = false;
    // --auto-answer-hud-prompts[=N] (THIS TASK, hud-prompt-autoanswer-
    // harness): this mission-drive loop has no keyboard/player-input concept
    // at all (it drives a mission's own Lua coroutines directly, tick by
    // tick - there is no "--no-input" flag here because there is no OTHER
    // mode; every run through this tool already IS the scripted/automated
    // case tools/sr3_viewer.cpp's own --no-input covers for its separate,
    // interactive --mission playtest path). A genuine player-input-gated
    // wait - e.g. m05.lua:1141's `while not M05_jumped do thread_yield() end`,
    // unblocked only by the registered callback a live
    // hud_prompt(target, promptId, callbackName) call names (spec-lua-api-
    // behaviour.md Sec13.3) - has no scripted way through this loop today,
    // so a mission stuck on one tick-budget-stalls regardless of how large
    // --tick-budget= is set, masking whatever that mission's own NEXT real
    // stall point is further down its script. When this flag is given, each
    // tick, any EngineState::hudPromptRequests() entry that has been showing
    // for at least N ticks (default 15; --auto-answer-hud-prompts=N
    // overrides; same semantics as --tick-budget='s own "=N" convention)
    // is answered by spawning a coroutine for its own registered callback
    // name via BareThreadTable::allocate() - the SAME "spawn, let the next
    // scheduler pass resume it" mechanism this file's own --probe-autopilot
    // dispatch block (isCallbackRegistrationNative family, further above)
    // already established for on_trigger/on_death/on_vehicle_enter, reused
    // here for hud_prompt rather than inventing a parallel mechanism -
    // generic over ANY currently showing prompt, not hardcoded to M05_JUMP.
    // Unlike --probe-autopilot (a post-hoc, one-shot diagnostic fired only
    // AFTER a mission is already classified still_suspended at budget
    // exhaustion), this fires LIVE, every tick, while the main tick loop is
    // still running - so a mission that is only blocked on a HUD prompt can
    // actually keep advancing within its own normal tick budget, rather than
    // needing a separate follow-up probe after the fact.
    bool autoAnswerHudPrompts = false;
    int hudPromptAutoAnswerDelayTicks = 15; // CHOSEN default, overridden by --auto-answer-hud-prompts=N
    bool probeAutopilotAutoRaisedRingSize = false;
    fs::path cacheDir;
    std::string registrationListPath;
    fs::path outDir;
    fs::path toolsDir;
    uint64_t maxScriptsToRun{};
    std::chrono::steady_clock::time_point t0;

    // --- setupHost (original lines 2168-2265) ---
    std::vector<RegisteredName> allNames;
    std::optional<Host> host_;
    std::string displayOption;
    std::string sceneTableSummary;
    std::string continuousExplosionTableSummary;
    std::string spawnRankHitPointsSummary;
    std::string weaponDefinitionTableSummary;
    std::string vehicleTypeClassTableSummary;

    // --- reportOpenStateAndHookCensus (original lines 2267-2344) ---
    std::string openStateSummary;
    const std::vector<HookSpec>* hooks_ = nullptr; // `hooks` (= confirmedHooks())
    std::vector<std::string> hookNamesAlsoRegisteredAsStubs;

    // --- walkArchives (original lines 2346-2378) ---
    std::vector<fs::path> archives;
    WalkState st;
    uint64_t grandBytes = 0;

    // --- setupVintDocs (original lines 2380-2441) ---
    std::function<std::string(std::string)> scriptKey; // was a local lambda; the document-context resolver keeps it
    std::vector<sr3vintdoc::Document> parsedDocs;
    std::vector<size_t> parsedDocSource; // index into st.docs
    std::unordered_map<std::string, size_t> docByScript; // scriptKey -> index into parsedDocs (first in walk order)
    std::vector<uint32_t> docHandleByParsed;
    uint64_t docsParseFailed = 0, docsWithoutScript = 0, docScriptDuplicates = 0, docsLoaded = 0;
    uint64_t docObjectsLoaded = 0, docPropertiesLoaded = 0, docContextHits = 0, docContextMisses = 0;

    // --- resolveStateTags (original lines 2443-2489) ---
    std::vector<statetag::Tag> scriptTag;
    uint64_t tagUi = 0, tagGameplay = 0, tagOpen = 0, tagConflict = 0;

    // --- runPreloads (original lines 2491-2643) ---
    PreloadResult pre_system_ui;
    PreloadResult pre_vint_lib;
    PreloadResult pre_game_ui_globals;
    PreloadResult pre_vdo_base;
    PreloadResult pre_vdo_anim;
    PreloadResult pre_vdo_input;
    PreloadResult pre_cell_foreground;
    PreloadResult pre_system_gp;
    std::vector<size_t> gameLibIndices;
    bool gameLibLoadedOk = false;
    bool gameLibRanOk = false;
    std::string gameLibLoadError, gameLibRunError;

    // --- runScriptPass (original lines 2645-2940) ---
    std::unordered_map<std::string, StaticRank> staticRank;
    uint64_t toRun{};
    uint64_t loadOkCount = 0, pcallOkCount_gp = 0, pcallOkCount_ui = 0;
    uint64_t agreeCount = 0, disagreeCount = 0; // sr3lua parse-clean vs real Lua load-clean
    std::unordered_map<std::string, HistHit> historical;
    uint64_t historicalTotal = 0;
    std::vector<HookAgg> hookAgg;
    uint64_t watchdogAbortCount = 0;
    // Real counts of (script, hooks-as-a-whole-state-pass) combinations
    // this task's own state-tag restriction skipped entirely (this task,
    // spec-lua-bindings.md Sec14.5) - i.e. how many scripts had their
    // gameplay-state or ui-state hook-firing pass skipped because their
    // own resolved tag pointed at the OTHER state. Each increment here
    // corresponds to `hooks.size()` real call attempts that did NOT
    // happen this run (previously always fired, HANDOFF.md Sec9.135's own
    // 21,041/32,852 baseline).
    uint64_t skippedGpFireCount = 0, skippedUiFireCount = 0;
    // Real counts of scripts whose own top-level runChunk() pass (this
    // task, step 0) was skipped entirely for one state because their own
    // resolved statetag pointed at the OTHER state - the new, parallel
    // restriction to the hook-firing skip counters just above.
    uint64_t skippedGpRunChunkCount = 0, skippedUiRunChunkCount = 0;
    uint64_t spec164Overrides = 0;

    // Real aggregate over every real hook-call error string this run
    // actually captures (gh.callError/uh.callError below), keyed by the
    // normalized message (stripLuaChunkLocationPrefix() above) - see this
    // tool's own top comment addition and the new
    // verdict_hook_error_messages.tsv output for the full rationale.
    std::unordered_map<std::string, uint64_t> errorMessageCounts;

    // --- writeStubHitReports (original lines 2942-3039) ---
    std::vector<std::pair<std::string, HistHit>> hitRows;
    std::vector<std::pair<std::string, sr3luahost::StubHit>> hitRowsWithHooks;
    uint64_t hooksEverDefinedCount = 0, hooksEverCalledOkCount = 0, hooksEverCalledErrCount = 0;
    std::vector<std::pair<std::string, uint64_t>> errorMessageRows;

    // --- setupMissionDrive (original lines 3357-3595) ---
    std::vector<MissionPackageRow> missionRows;
    std::vector<MissionPackageRow> missions; // deduped, in first-seen TSV order
    int kMissionTickBudget{}; // CHOSEN (default 20, --tick-budget= override) - see setupMissionDrive()
    std::vector<MissionResult> missionResults;
    uint64_t schedulerForeignErrorsTotal = 0; // leftover records' own errors on a resume (not in any mission's row)
    // --auto-answer-hud-prompts (THIS TASK): persists across the WHOLE
    // mission loop below (same reason schedulerForeignErrorsTotal does,
    // immediately above) - EngineState::hudPromptRequests() is a single
    // host-wide vector that only ever grows (same convention as
    // tools/sr3_viewer.cpp's own identical tracker), never reset per
    // mission, so these must not be either (resetting per mission would
    // re-dispatch an already-answered earlier mission's own old requests).
    // globalTick increments once per tick of every mission in this run (NOT
    // the per-mission `tick` loop variable, which restarts at 1 each
    // mission) so "N ticks since first seen" stays correct even if a prompt
    // happens to still be unanswered at a mission boundary (should not
    // happen in practice within a sane delay, but is harmless either way).
    size_t hudPromptRespondedCount = 0;
    std::vector<long long> hudPromptFirstSeenGlobalTick;
    long long globalTick = 0;
    std::unordered_map<std::string, uint64_t> preMissionCounts;
};

// ---- main()'s sections, called by main() in this order (parseArgs returns -1 to continue, else the exit code) ----
int parseArgs(LuaHostRunSession& sess, int argc, char** argv);   // setup_args.cpp
void setupHost(LuaHostRunSession& sess);                          // setup_host.cpp
void reportOpenStateAndHookCensus(LuaHostRunSession& sess);       // setup_host.cpp
void walkArchives(LuaHostRunSession& sess);                       // setup_host.cpp
void setupVintDocs(LuaHostRunSession& sess);                      // setup_host.cpp
void resolveStateTags(LuaHostRunSession& sess);                   // setup_host.cpp
void runPreloads(LuaHostRunSession& sess);                        // setup_host.cpp
void runScriptPass(LuaHostRunSession& sess);                      // script_pass.cpp
void writeStubHitReports(LuaHostRunSession& sess);                // script_pass.cpp
void writeSummary(LuaHostRunSession& sess);                       // summary.cpp
void setupMissionDrive(LuaHostRunSession& sess);                  // mission_drive.cpp
void driveMissions(LuaHostRunSession& sess);                      // mission_drive.cpp
void reportMissionDrive(LuaHostRunSession& sess);                 // mission_report.cpp

} // namespace luahostrun
